#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include "remote-client.hpp"
#include "update-controller.hpp"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QLabel>
#include <QHash>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSslSocket>
#include <QSpinBox>
#include <QTimer>
#include <QTableWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QUuid>
#include <cmath>
#include <memory>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void) { return "Excel Interactio: custom Twitch chat interactions for OBS"; }

static fx::RemoteClient *remote = nullptr;
static fx::UpdateController *updater = nullptr;
static QTimer *ruleTimer = nullptr;
static QJsonArray commands;
static QHash<QString, qint64> lastRun;
static QHash<QString, quint64> generations;
static QHash<int, qint64> timerNext;
static void styleBuilder(QDialog &window)
{
    window.setStyleSheet(R"(
        QDialog { background: #11151f; color: #e5eafa; font-family: 'Cascadia Code', 'Consolas', monospace; font-size: 12px; }
        QLabel { color: #b6c2da; background: transparent; }
        QLabel#title { color: #f0f4ff; font-size: 19px; font-weight: 700; }
        QLabel#muted { color: #8291ad; }
        QLabel#ifKeyword { color: #c68bff; font-size: 15px; font-weight: 700; }
        QLabel#thenKeyword { color: #62d8cb; font-size: 15px; font-weight: 700; }
        QLabel#codePreview { color: #d8e1f5; background: #101722; border: 1px solid #2e405b; border-radius: 7px; padding: 12px; }
        QFrame#codeBlock { background: #1a2230; border: 1px solid #33415a; border-radius: 8px; }
        QFrame#connectionBlock { background: #192330; border: 1px solid #33415a; border-radius: 8px; }
        QLineEdit, QComboBox, QSpinBox { background: #101722; color: #e6eeff; border: 1px solid #40506c; border-radius: 5px; padding: 7px; min-height: 18px; selection-background-color: #38577b; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #71b6ff; }
        QComboBox QAbstractItemView { background: #192331; color: #e6eeff; selection-background-color: #38577b; }
        QListWidget { background: #131b28; color: #e6eeff; border: 1px solid #33415a; border-radius: 8px; padding: 5px; outline: 0; }
        QListWidget::item { border-radius: 5px; padding: 5px; }
        QListWidget::item:selected { background: #30435c; }
        QPushButton { background: #28374a; color: #e7f0ff; border: 1px solid #405470; border-radius: 5px; padding: 8px 14px; }
        QPushButton:hover { background: #344963; border-color: #78a4dc; }
        QPushButton:disabled { color: #72819a; background: #242b35; }
        QPushButton#primary { background: #365c85; border-color: #6191bf; color: white; font-weight: 700; }
    )");
}
struct ActiveOverlay { QString sceneUuid; int64_t itemId; };
static QList<ActiveOverlay> activeOverlays;
static QString configPath() { char *p = obs_module_config_path("settings.json"); QString s = QString::fromUtf8(p); bfree(p); return s; }
static QString videoId(const QString &text)
{
    QUrl u(text); QString host = u.host().toLower(), id;
    if (host == "youtu.be") id = u.path().mid(1).section('/', 0, 0);
    else if (host == "youtube.com" || host == "www.youtube.com" || host == "m.youtube.com") {
        if (u.path() == "/watch") id = QUrlQuery(u).queryItemValue("v");
        else if (u.path().startsWith("/shorts/") || u.path().startsWith("/embed/")) id = u.path().section('/', 2, 2);
    }
    static const QRegularExpression valid("^[A-Za-z0-9_-]{11}$");
    return valid.match(id).hasMatch() ? id : QString();
}
static void sendMessage(QString message)
{
    message.replace('\r', ' '); message.replace('\n', ' ');
    if (!remote || !remote->sendChat(message.left(400))) blog(LOG_WARNING, "[Excel Interactio] Chat message was not sent: ExcelProtocol is not connected");
}
static void removeOverlay(const QString &sceneUuid, int64_t overlayId)
{
    obs_source_t *s = obs_get_source_by_uuid(sceneUuid.toUtf8().constData());
    obs_scene_t *scene = s ? obs_scene_from_source(s) : nullptr;
    auto *item = scene ? obs_scene_find_sceneitem_by_id(scene, overlayId) : nullptr;
    if (item) obs_sceneitem_remove(item);
    if (s) obs_source_release(s);
    for (qsizetype i = activeOverlays.size(); i-- > 0;) if (activeOverlays[i].sceneUuid == sceneUuid && activeOverlays[i].itemId == overlayId) activeOverlays.removeAt(i);
}
static bool matchOverlay(obs_scene_t *scene, int64_t targetId, int64_t overlayId)
{
    auto *target = obs_scene_find_sceneitem_by_id(scene, targetId);
    auto *overlay = obs_scene_find_sceneitem_by_id(scene, overlayId);
    if (!target || !overlay) return false;
    struct matrix4 box; obs_sceneitem_get_box_transform(target, &box);
    float width = std::hypot(box.x.x, box.x.y), height = std::hypot(box.y.x, box.y.y);
    if (width < 1 || height < 1) { obs_sceneitem_set_visible(overlay, false); return true; }
    struct obs_transform_info info{};
    info.pos.x = box.t.x; info.pos.y = box.t.y;
    info.rot = std::atan2(box.x.y, box.x.x) * 180.0f / 3.14159265358979323846f;
    info.scale.x = 1.0f; info.scale.y = (box.x.x * box.y.y - box.x.y * box.y.x) < 0 ? -1.0f : 1.0f;
    info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
    info.bounds_type = OBS_BOUNDS_STRETCH; info.bounds_alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
    info.bounds.x = width; info.bounds.y = height;
    obs_sceneitem_set_info2(overlay, &info);
    obs_sceneitem_set_visible(overlay, obs_sceneitem_visible(target));
    return true;
}
static bool playOnTarget(const QJsonObject &c)
{
    QString sceneUuid = c.value("scene_uuid").toString();
    bool validId = false; int64_t targetId = c.value("target_item_id").toString().toLongLong(&validId);
    QString media = c.value("media").toString(); QString yt = videoId(media);
    const QString preset = media.startsWith("builtin:") ? media.mid(8) : QString();
    static const QSet<QString> allowedPresets{"neon", "confetti", "sparks", "glitch"};
    char *builtinPath = allowedPresets.contains(preset) ? obs_module_file(("overlays/" + preset + ".html").toUtf8().constData()) : nullptr;
    obs_source_t *sceneSource = obs_get_source_by_uuid(sceneUuid.toUtf8().constData());
    obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
    auto *target = scene && validId ? obs_scene_find_sceneitem_by_id(scene, targetId) : nullptr;
    if (!target || (!builtinPath && !QFile::exists(media) && yt.isEmpty())) { if (builtinPath) bfree(builtinPath); if (sceneSource) obs_source_release(sceneSource); return false; }
    obs_data_t *settings = obs_data_create();
    const char *type;
    if (builtinPath) {
        type = "browser_source"; obs_data_set_bool(settings, "is_local_file", true);
        obs_data_set_string(settings, "local_file", builtinPath); bfree(builtinPath);
        obs_data_set_int(settings, "width", 960); obs_data_set_int(settings, "height", 540);
    } else if (!yt.isEmpty()) {
        type = "browser_source";
        QString url = "https://www.youtube.com/embed/" + yt + "?autoplay=1&playsinline=1";
        obs_data_set_string(settings, "url", url.toUtf8().constData());
        obs_data_set_int(settings, "width", 1920); obs_data_set_int(settings, "height", 1080);
    } else {
        type = "ffmpeg_source"; obs_data_set_bool(settings, "is_local_file", true);
        obs_data_set_string(settings, "local_file", media.toUtf8().constData());
        obs_data_set_bool(settings, "restart_on_activate", true); obs_data_set_bool(settings, "looping", false);
    }
    QString name = "Excel Interactio overlay " + QUuid::createUuid().toString(QUuid::WithoutBraces);
    obs_source_t *source = obs_source_create_private(type, name.toUtf8().constData(), settings); obs_data_release(settings);
    if (!source) { obs_source_release(sceneSource); return false; }
    obs_sceneitem_t *overlay = obs_scene_add(scene, source); obs_source_release(source);
    if (!overlay) { obs_source_release(sceneSource); return false; }
    int64_t overlayId = obs_sceneitem_get_id(overlay);
    obs_sceneitem_set_order_position(overlay, obs_sceneitem_get_order_position(target) + 1);
    matchOverlay(scene, targetId, overlayId);
    activeOverlays.append({sceneUuid, overlayId});
    int duration = qBound(1, c.value("duration_seconds").toInt(5), 3600);
    QTimer *follow = new QTimer(remote); follow->setInterval(100);
    QObject::connect(follow, &QTimer::timeout, remote, [sceneUuid, targetId, overlayId, follow] {
        obs_source_t *s = obs_get_source_by_uuid(sceneUuid.toUtf8().constData());
        obs_scene_t *sc = s ? obs_scene_from_source(s) : nullptr;
        bool found = sc && matchOverlay(sc, targetId, overlayId);
        if (s) obs_source_release(s);
        if (!found) { removeOverlay(sceneUuid, overlayId); follow->stop(); follow->deleteLater(); }
    });
    follow->start();
    QTimer::singleShot(duration * 1000, follow, [sceneUuid, overlayId, follow] { removeOverlay(sceneUuid, overlayId); follow->stop(); follow->deleteLater(); });
    obs_source_media_restart(obs_sceneitem_get_source(overlay));
    obs_source_release(sceneSource); return true;
}
static void runCommand(const QString &message)
{
    QString word = message.trimmed().section(' ', 0, 0).toLower();
    for (const auto &value : commands) {
        QJsonObject c = value.toObject();
        if (c.value("trigger_type").toString("command") != "command") continue;
        if (c.value("command").toString().toLower() != word) continue;
        if (c.value("action").toString("overlay") == "message") {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            int cooldown = qBound(0, c.value("cooldown_seconds").toInt(10), 86400);
            if (lastRun.contains(word) && now - lastRun.value(word) < cooldown * 1000LL) return;
            lastRun[word] = now; sendMessage(c.value("message").toString()); return;
        }
        if (c.contains("target_item_id")) {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            int cooldown = qBound(0, c.value("cooldown_seconds").toInt(10), 86400);
            if (lastRun.contains(word) && now - lastRun.value(word) < cooldown * 1000LL) return;
            if (playOnTarget(c)) lastRun[word] = now;
            else blog(LOG_WARNING, "[Excel Interactio] Could not create overlay: target or media unavailable");
            return;
        }
        QString sceneName = c.value("scene").toString(), sourceName = c.value("source").toString();
        QString key = sceneName + QChar(0x1f) + sourceName;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        int cooldown = qBound(0, c.value("cooldown_seconds").toInt(10), 86400);
        if (lastRun.contains(word) && now - lastRun.value(word) < cooldown * 1000LL) return;
        obs_source_t *sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
        obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
        obs_sceneitem_t *item = scene ? obs_scene_find_source(scene, sourceName.toUtf8().constData()) : nullptr;
        if (!item) { blog(LOG_WARNING, "[chat-overlays] Scene or source not found"); if (sceneSource) obs_source_release(sceneSource); return; }
        obs_source_t *source = obs_sceneitem_get_source(item);
        QString media = c.value("media").toString(); QString type = QString::fromUtf8(obs_source_get_unversioned_id(source));
        if (!media.isEmpty()) {
            obs_data_t *settings = obs_source_get_settings(source);
            if (type == "ffmpeg_source" && QFile::exists(media)) {
                obs_data_set_bool(settings, "is_local_file", true);
                obs_data_set_string(settings, "local_file", media.toUtf8().constData());
            } else if (type == "browser_source" && !videoId(media).isEmpty()) {
                QString url = "https://www.youtube.com/embed/" + videoId(media) + "?autoplay=1&playsinline=1";
                obs_data_set_string(settings, "url", url.toUtf8().constData());
            } else {
                blog(LOG_WARNING, "[chat-overlays] Invalid media or incompatible source type");
                obs_data_release(settings); obs_source_release(sceneSource); return;
            }
            obs_source_update(source, settings); obs_data_release(settings);
        }
        lastRun[word] = now;
        quint64 generation = ++generations[key];
        obs_sceneitem_set_visible(item, true);
        if (type == "ffmpeg_source") obs_source_media_restart(source);
        int duration = qBound(1, c.value("duration_seconds").toInt(5), 3600);
        QTimer::singleShot(duration * 1000, remote, [sceneName, sourceName, key, generation]() {
            if (generations.value(key) != generation) return;
            obs_source_t *s = obs_get_source_by_name(sceneName.toUtf8().constData());
            obs_scene_t *sc = s ? obs_scene_from_source(s) : nullptr;
            obs_sceneitem_t *i = sc ? obs_scene_find_source(sc, sourceName.toUtf8().constData()) : nullptr;
            if (i) obs_sceneitem_set_visible(i, false);
            if (s) obs_source_release(s);
        });
        obs_source_release(sceneSource); return;
    }
}
static bool editRule(QWidget *parent, QJsonObject &rule)
{
    QDialog editor(parent); editor.setWindowTitle("Excel Interactio • Rule builder"); editor.resize(720, 690); styleBuilder(editor);
    auto *layout = new QVBoxLayout(&editor); layout->setContentsMargins(22, 20, 22, 20); layout->setSpacing(12);
    auto *title = new QLabel("Rule builder"); title->setObjectName("title"); layout->addWidget(title);
    auto *subtitle = new QLabel("// Choose a trigger and connect it to an action"); subtitle->setObjectName("muted"); layout->addWidget(subtitle);
    auto *ifBlock = new QFrame; ifBlock->setObjectName("codeBlock"); auto *ifLayout = new QVBoxLayout(ifBlock); ifLayout->setContentsMargins(15, 13, 15, 15);
    auto *ifLabel = new QLabel("IF  <trigger> {"); ifLabel->setObjectName("ifKeyword"); ifLayout->addWidget(ifLabel);
    auto *ifForm = new QFormLayout; ifForm->setHorizontalSpacing(18); ifForm->setVerticalSpacing(8); ifLayout->addLayout(ifForm); layout->addWidget(ifBlock);
    auto *thenBlock = new QFrame; thenBlock->setObjectName("codeBlock"); auto *thenLayout = new QVBoxLayout(thenBlock); thenLayout->setContentsMargins(15, 13, 15, 15);
    auto *thenLabel = new QLabel("} THEN  <action> {"); thenLabel->setObjectName("thenKeyword"); thenLayout->addWidget(thenLabel);
    auto *thenForm = new QFormLayout; thenForm->setHorizontalSpacing(18); thenForm->setVerticalSpacing(8); thenLayout->addLayout(thenForm); layout->addWidget(thenBlock);
    QComboBox trigger; trigger.addItem("Chat command", "command"); trigger.addItem("Repeating timer", "timer");
    trigger.setCurrentIndex(qMax(0, trigger.findData(rule.value("trigger_type").toString("command"))));
    QLineEdit command(rule.value("command").toString("!example"));
    QSpinBox minutes; minutes.setRange(1, 10080); minutes.setValue(rule.value("minutes").toInt(10)); minutes.setSuffix(" minutes");
    QComboBox action; action.addItem("Play overlay", "overlay"); action.addItem("Send chat message", "message");
    action.setCurrentIndex(qMax(0, action.findData(rule.value("action").toString("overlay"))));
    QComboBox target; target.addItem("Choose a source…");
    struct obs_frontend_source_list scenes{}; obs_frontend_get_scenes(&scenes);
    for (size_t i = 0; i < scenes.sources.num; ++i) {
        obs_source_t *sceneSource = scenes.sources.array[i]; obs_scene_t *scene = obs_scene_from_source(sceneSource);
        if (!scene) continue;
        struct Collector { QComboBox *target; obs_source_t *sceneSource; } collector{&target, sceneSource};
        obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
            auto *c = static_cast<Collector *>(opaque); obs_source_t *s = obs_sceneitem_get_source(item);
            if (!s || obs_sceneitem_is_group(item)) return true;
            QString sceneName = QString::fromUtf8(obs_source_get_name(c->sceneSource));
            QString sourceName = QString::fromUtf8(obs_source_get_name(s));
            if (sourceName.startsWith("Excel Interactio overlay ")) return true;
            QVariantMap data{{"scene_uuid", QString::fromUtf8(obs_source_get_uuid(c->sceneSource))}, {"scene", sceneName}, {"source", sourceName}, {"target_item_id", QString::number(obs_sceneitem_get_id(item))}};
            c->target->addItem(sceneName + " — " + sourceName, data); return true;
        }, &collector);
    }
    obs_frontend_source_list_free(&scenes);
    for (int i = 1; i < target.count(); ++i) {
        auto d = target.itemData(i).toMap();
        if (d.value("scene_uuid").toString() == rule.value("scene_uuid").toString() && d.value("target_item_id").toString() == rule.value("target_item_id").toString()) { target.setCurrentIndex(i); break; }
    }
    QLineEdit content(rule.value("action").toString("overlay") == "message" ? rule.value("message").toString() : rule.value("media").toString());
    QComboBox overlayChoice; overlayChoice.addItem("Use my file or YouTube link", "custom");
    overlayChoice.addItem("Neon frame", "builtin:neon"); overlayChoice.addItem("Celebration confetti", "builtin:confetti");
    overlayChoice.addItem("Twinkling sparks", "builtin:sparks"); overlayChoice.addItem("Digital glitch", "builtin:glitch");
    const int presetIndex = overlayChoice.findData(rule.value("media").toString()); if (presetIndex >= 0) { overlayChoice.setCurrentIndex(presetIndex); content.clear(); }
    QPushButton browse("Choose local file…");
    QSpinBox duration; duration.setRange(1, 3600); duration.setValue(rule.value("duration_seconds").toInt(5)); duration.setSuffix(" seconds");
    QSpinBox cooldown; cooldown.setRange(0, 86400); cooldown.setValue(rule.value("cooldown_seconds").toInt(10)); cooldown.setSuffix(" seconds");
    ifForm->addRow("Trigger", &trigger); ifForm->addRow("Command", &command); ifForm->addRow("Repeat every", &minutes); ifForm->addRow("Cooldown", &cooldown);
    thenForm->addRow("Action", &action); thenForm->addRow("Play over source", &target); thenForm->addRow("Overlay", &overlayChoice);
    thenForm->addRow("Content", &content); thenForm->addRow("", &browse); thenForm->addRow("Overlay duration", &duration);
    auto *browseSite = new QLabel("<a style='color:#79c8ff' href='https://kudos.tv/collections/freebies'>Find free overlays ↗</a>"); browseSite->setOpenExternalLinks(true);
    thenForm->addRow("", browseSite);
    auto *closing = new QLabel("}"); closing->setObjectName("thenKeyword"); layout->addWidget(closing);
    auto *preview = new QLabel; preview->setObjectName("codePreview"); preview->setWordWrap(true); preview->setTextFormat(Qt::RichText); layout->addWidget(preview);
    auto updateFields = [&] {
        bool timed = trigger.currentData().toString() == "timer";
        if (timed && action.currentData().toString() != "message") action.setCurrentIndex(action.findData("message"));
        bool overlay = action.currentData().toString() == "overlay";
        ifForm->setRowVisible(&command, !timed); ifForm->setRowVisible(&minutes, timed); ifForm->setRowVisible(&cooldown, !timed);
        const bool custom = overlayChoice.currentData().toString() == "custom";
        thenForm->setRowVisible(&target, overlay); thenForm->setRowVisible(&overlayChoice, overlay); thenForm->setRowVisible(&duration, overlay); thenForm->setRowVisible(&browse, overlay && custom);
        thenForm->setRowVisible(&content, !overlay || custom); thenForm->setRowVisible(browseSite, overlay);
        if (auto *label = qobject_cast<QLabel *>(thenForm->labelForField(&content))) label->setText(overlay ? "File path / YouTube link" : "Chat message");
        QString condition = timed ? QString("TIMER(%1 min)").arg(minutes.value()) : QString("CHAT(\"%1\")").arg(command.text().toHtmlEscaped());
        QString result = overlay ? QString("PLAY %1 on \"%2\"").arg(overlayChoice.currentText().toHtmlEscaped(), target.currentText().toHtmlEscaped()) : QString("SEND \"%1\"").arg(content.text().toHtmlEscaped());
        preview->setText(QString("<span style='color:#8291ad'>// live preview</span><br><span style='color:#c68bff'><b>IF</b></span> %1<br><span style='color:#62d8cb'><b>THEN</b></span> %2").arg(condition, result));
    };
    QObject::connect(&trigger, &QComboBox::currentIndexChanged, &editor, [&](int) { updateFields(); });
    QObject::connect(&action, &QComboBox::currentIndexChanged, &editor, [&](int) { updateFields(); });
    QObject::connect(&command, &QLineEdit::textChanged, &editor, [&](const QString &) { updateFields(); });
    QObject::connect(&content, &QLineEdit::textChanged, &editor, [&](const QString &) { updateFields(); });
    QObject::connect(&minutes, &QSpinBox::valueChanged, &editor, [&](int) { updateFields(); });
    QObject::connect(&target, &QComboBox::currentIndexChanged, &editor, [&](int) { updateFields(); });
    QObject::connect(&overlayChoice, &QComboBox::currentIndexChanged, &editor, [&](int) { updateFields(); });
    QObject::connect(&browse, &QPushButton::clicked, &editor, [&] { QString path = QFileDialog::getOpenFileName(&editor, "Choose overlay file"); if (!path.isEmpty()) content.setText(path); });
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &editor, [&] {
        bool timed = trigger.currentData().toString() == "timer", overlay = action.currentData().toString() == "overlay";
        if ((!timed && !command.text().trimmed().startsWith('!')) || (overlay && (target.currentIndex() < 1 || (overlayChoice.currentData().toString() == "custom" && content.text().trimmed().isEmpty()))) || (!overlay && content.text().trimmed().isEmpty())) {
            QMessageBox::warning(&editor, "Check rule", "Enter a command starting with !, choose a source and media for overlays, or enter a chat message."); return;
        }
        rule = QJsonObject{{"trigger_type", trigger.currentData().toString()}, {"action", action.currentData().toString()}, {"command", command.text().trimmed().toLower()}, {"minutes", minutes.value()}, {"duration_seconds", duration.value()}, {"cooldown_seconds", cooldown.value()}};
        if (overlay) { auto d = target.currentData().toMap(); rule["scene_uuid"] = d.value("scene_uuid").toString(); rule["scene"] = d.value("scene").toString(); rule["source"] = d.value("source").toString(); rule["target_item_id"] = d.value("target_item_id").toString(); }
        rule[overlay ? "media" : "message"] = overlay && overlayChoice.currentData().toString() != "custom" ? overlayChoice.currentData().toString() : content.text().trimmed(); editor.accept();
    });
    updateFields(); return editor.exec() == QDialog::Accepted;
}
static void configure()
{
    QDialog dialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
    dialog.setWindowTitle("Excel Interactio"); dialog.resize(820, 650); styleBuilder(dialog);
    auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(22, 20, 22, 20); layout->setSpacing(12);
    auto *heading = new QLabel("Excel Interactio"); heading->setObjectName("title"); layout->addWidget(heading);
    auto *intro = new QLabel("// Create interactions by connecting triggers to actions"); intro->setObjectName("muted"); layout->addWidget(intro);
    auto *connection = new QFrame; connection->setObjectName("connectionBlock"); auto *connectionLayout = new QHBoxLayout(connection); connectionLayout->setContentsMargins(13, 10, 13, 10);
    auto *connectionText = new QVBoxLayout; auto *connectionTitle = new QLabel("EXCELPROTOCOL"); connectionTitle->setObjectName("thenKeyword");
    QLabel status; connectionText->addWidget(connectionTitle); connectionText->addWidget(&status); connectionLayout->addLayout(connectionText, 1);
    auto *pair = new QPushButton("Pair device"); auto *forget = new QPushButton("Forget"); connectionLayout->addWidget(pair); connectionLayout->addWidget(forget); layout->addWidget(connection);
    QTimer statusTimer; statusTimer.setInterval(500); QObject::connect(&statusTimer, &QTimer::timeout, &dialog, [&] { status.setText(remote ? remote->statusText() : "Unavailable"); }); statusTimer.start();
    status.setText(remote ? remote->statusText() : "Unavailable");
    QObject::connect(pair, &QPushButton::clicked, &dialog, [&] {
        bool ok = false; QString code = QInputDialog::getText(&dialog, "Pair with ExcelProtocol", "Enter a pairing code from the ExcelProtocol dashboard:", QLineEdit::Normal, {}, &ok);
        if (ok && remote) remote->pair(code, "Excel Interactio");
    });
    QObject::connect(forget, &QPushButton::clicked, &dialog, [&] { if (remote && QMessageBox::question(&dialog, "Forget device?", "Remove the local pairing? Revoke the device in ExcelProtocol as well if needed.", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) remote->forgetDevice(); });
    auto *updatesButton = new QPushButton("Updates…"); connectionLayout->addWidget(updatesButton);
    QObject::connect(updatesButton, &QPushButton::clicked, &dialog, [&] { if (!updater) return; QDialog updates(&dialog); updates.setWindowTitle("Excel Interactio • Updates"); updates.resize(650, 350); styleBuilder(updates); auto *updatesLayout = new QVBoxLayout(&updates); updatesLayout->addWidget(updater->createWidget(&updates)); updates.exec(); });
    auto *rulesHeader = new QHBoxLayout; auto *rulesTitle = new QLabel("rules.json  // your interactions"); rulesTitle->setObjectName("ifKeyword"); rulesHeader->addWidget(rulesTitle); rulesHeader->addStretch();
    auto *add = new QPushButton("+ Add rule"); add->setObjectName("primary"); rulesHeader->addWidget(add); layout->addLayout(rulesHeader);
    QListWidget list; layout->addWidget(&list, 1); QJsonArray rules = commands;
    auto refresh = [&] {
        list.clear();
        for (const auto &value : rules) {
            auto c = value.toObject(); QString when = c.value("trigger_type").toString("command") == "timer" ? QString("TIMER (%1 min)").arg(c.value("minutes").toInt(10)) : c.value("command").toString();
            bool message = c.value("action").toString("overlay") == "message";
            QString then = message ? QString("SEND  \"%1\"").arg(c.value("message").toString().toHtmlEscaped()) : QString("PLAY  overlay on \"%1\"").arg(c.value("source").toString().toHtmlEscaped());
            auto *item = new QListWidgetItem(&list); item->setSizeHint(QSize(0, 52));
            auto *label = new QLabel; label->setTextFormat(Qt::RichText); label->setContentsMargins(10, 3, 10, 3); label->setAttribute(Qt::WA_TransparentForMouseEvents);
            label->setText(QString("<span style='color:#c68bff'><b>IF</b></span> <span style='color:#d7e3fa'>%1</span><br><span style='color:#62d8cb'><b>THEN</b></span> <span style='color:#d7e3fa'>%2</span>").arg(when.toHtmlEscaped(), then));
            list.setItemWidget(item, label);
        }
    };
    refresh();
    auto *actions = new QHBoxLayout; auto *edit = new QPushButton("Edit"); auto *test = new QPushButton("Test overlay"); auto *remove = new QPushButton("Delete");
    actions->addWidget(edit); actions->addWidget(test); actions->addWidget(remove); actions->addStretch(); layout->addLayout(actions);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] { QJsonObject c; if (editRule(&dialog, c)) { rules.append(c); refresh(); list.setCurrentRow(rules.size()-1); } });
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] { int r=list.currentRow(); if (r < 0 || r >= rules.size()) return; QJsonObject c=rules[r].toObject(); if (editRule(&dialog, c)) { rules[r]=c; refresh(); list.setCurrentRow(r); } });
    QObject::connect(test, &QPushButton::clicked, &dialog, [&] { int r=list.currentRow(); if (r < 0 || r >= rules.size()) return; auto c=rules[r].toObject(); if (c.value("action").toString() != "overlay" || !c.contains("target_item_id") || !playOnTarget(c)) QMessageBox::warning(&dialog, "Cannot test overlay", "Select an overlay rule with a valid source and media file or link."); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] { int r=list.currentRow(); if (r < 0 || r >= rules.size()) return; if (QMessageBox::question(&dialog, "Delete rule?", "Delete the selected rule?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) { rules.removeAt(r); refresh(); } });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    commands = rules; lastRun.clear(); timerNext.clear();
    QJsonObject root{{"commands", commands}};
    QDir().mkpath(QFileInfo(configPath()).absolutePath());
    QFile file(configPath()); if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { file.write(QJsonDocument(root).toJson()); file.close(); }
}
bool obs_module_load(void)
{
    char *tlsFile = obs_module_file("tls/qschannelbackend.dll");
    if (tlsFile) {
        QDir tlsDir = QFileInfo(QString::fromUtf8(tlsFile)).dir(); tlsDir.cdUp();
        QCoreApplication::addLibraryPath(tlsDir.absolutePath()); bfree(tlsFile);
    }
    blog(QSslSocket::supportsSsl() ? LOG_INFO : LOG_WARNING, "[Excel Interactio] TLS backend %s", QSslSocket::supportsSsl() ? "available" : "unavailable");
    QFile file(configPath()); if (file.open(QIODevice::ReadOnly)) { auto o = QJsonDocument::fromJson(file.readAll()).object(); commands=o["commands"].toArray(); }
    remote = new fx::RemoteClient(qApp);
    updater = new fx::UpdateController(qApp); updater->start();
    remote->onChatMessage = [](const QString &message) { runCommand(message); };
    remote->onChatSendResult = [](bool ok, const QString &error) { if (!ok) blog(LOG_WARNING, "[Excel Interactio] ExcelProtocol could not send the message: %s", error.toUtf8().constData()); };
    ruleTimer = new QTimer(remote);
    QObject::connect(ruleTimer, &QTimer::timeout, remote, [] {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int i=0; i<commands.size(); ++i) {
            auto c=commands[i].toObject(); if (c["trigger_type"].toString() != "timer" || c["action"].toString() != "message") continue;
            qint64 interval=qBound(1, c["minutes"].toInt(10), 10080)*60000LL;
            if (!timerNext.contains(i)) timerNext[i] = now + interval;
            else if (now >= timerNext[i]) { sendMessage(c["message"].toString()); timerNext[i] = now + interval; }
        }
    }); ruleTimer->start(1000);
    obs_frontend_add_tools_menu_item("Excel Interactio", [](void *) { configure(); }, nullptr);
    QTimer::singleShot(0, remote, [] { remote->start(); }); return true;
}
void obs_module_unload(void) { while (!activeOverlays.isEmpty()) { auto item = activeOverlays.last(); removeOverlay(item.sceneUuid, item.itemId); } if (updater) { updater->shutdown(); delete updater; updater=nullptr; } if (remote) { remote->shutdown(); delete remote; remote=nullptr; ruleTimer=nullptr; } }

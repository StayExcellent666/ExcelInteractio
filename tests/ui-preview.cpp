#include "../src/plugin.cpp"
#include <QScreen>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (!obs_startup("en-US", nullptr, nullptr)) return 1;
    QJsonObject rule = argc > 1 ? QJsonObject{{"trigger_type", "command"}, {"action", "overlay"}, {"command", "!fire"}, {"media", "builtin:neon"}} : QJsonObject{{"trigger_type", "timer"}, {"action", "message"}, {"minutes", 10}, {"message", "Hey people!"}};
    QTimer::singleShot(500, &app, [&] {
        QWidget *window = QApplication::activeModalWidget();
        if (!window) return;
        window->grab().save(argc > 1 ? "ui-overlay-preview.png" : "ui-preview.png");
        if (auto *dialog = qobject_cast<QDialog *>(window)) dialog->reject();
    });
    editRule(nullptr, rule);
    obs_shutdown();
    return 0;
}

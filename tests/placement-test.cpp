#include "../src/plugin.cpp"
#include <cstdio>
#include <cmath>

static const char *testName(void *) { return "Placement test"; }
static void *testCreate(obs_data_t *, obs_source_t *) { return reinterpret_cast<void *>(1); }
static void testDestroy(void *) {}
static uint32_t testWidth(void *) { return 640; }
static uint32_t testHeight(void *) { return 480; }
static bool close(float a, float b) { return std::fabs(a - b) < 2.0f; }

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (!obs_startup("en-US", nullptr, nullptr)) return 1;
    obs_add_data_path("C:/obs-studio/data/libobs/");
    obs_video_info video{}; video.graphics_module = "C:/obs-studio/bin/64bit/libobs-d3d11.dll"; video.fps_num = 30; video.fps_den = 1;
    video.base_width = 1920; video.base_height = 1080; video.output_width = 1920; video.output_height = 1080;
    video.output_format = VIDEO_FORMAT_NV12; video.gpu_conversion = true;
    if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) return 5;
    obs_source_info sourceInfo{};
    sourceInfo.id = "interactio_test_source"; sourceInfo.type = OBS_SOURCE_TYPE_INPUT;
    sourceInfo.output_flags = OBS_SOURCE_VIDEO; sourceInfo.get_name = testName;
    sourceInfo.create = testCreate; sourceInfo.destroy = testDestroy;
    sourceInfo.get_width = testWidth; sourceInfo.get_height = testHeight;
    obs_register_source(&sourceInfo);
    obs_scene_t *scene = obs_scene_create("placement-test-scene");
    obs_source_t *one = obs_source_create_private(sourceInfo.id, "camera", nullptr);
    obs_source_t *two = obs_source_create_private(sourceInfo.id, "overlay", nullptr);
    if (!scene || !one || !two) return 2;
    obs_sceneitem_t *target = obs_scene_add(scene, one), *overlay = obs_scene_add(scene, two);
    struct vec2 pos{150, 90}, scale{0.6f, 0.75f};
    obs_sceneitem_set_pos(target, &pos); obs_sceneitem_set_scale(target, &scale); obs_sceneitem_set_rot(target, 25.0f);
    struct obs_sceneitem_crop crop{20, 30, 40, 50}; obs_sceneitem_set_crop(target, &crop);
    if (!matchOverlay(scene, obs_sceneitem_get_id(target), obs_sceneitem_get_id(overlay))) return 3;
    struct matrix4 a{}, b{}; obs_sceneitem_get_box_transform(target, &a); obs_sceneitem_get_box_transform(overlay, &b);
    std::printf("target %.2f %.2f %.2f %.2f %.2f %.2f; overlay %.2f %.2f %.2f %.2f %.2f %.2f\n", a.x.x,a.x.y,a.y.x,a.y.y,a.t.x,a.t.y,b.x.x,b.x.y,b.y.x,b.y.y,b.t.x,b.t.y);
    bool matches = close(a.x.x,b.x.x) && close(a.x.y,b.x.y) && close(a.y.x,b.y.x) && close(a.y.y,b.y.y) && close(a.t.x,b.t.x) && close(a.t.y,b.t.y);
    std::printf("%s\n", matches ? "Cropped, scaled and rotated target bounds matched" : "Target bounds did not match");
    obs_sceneitem_remove(target); obs_sceneitem_remove(overlay); obs_source_release(one); obs_source_release(two); obs_scene_release(scene); obs_shutdown();
    return matches ? 0 : 4;
}

#include "nova.h"
#include <raylib.h>
#include <string.h>

/* ── Resource pools ── */
static Texture2D textures[256];
static int texture_count = 0;

static Sound sounds[256];
static int sound_count = 0;

static Music musics[64];
static int music_count = 0;

static Model models[128];
static int model_count = 0;

/* ── Color conversion helpers ── */

static Color nova_to_color(NovaValue v, int line) {
    if (!IS_DICT(v))
        nova_type_error(line, "expected a color dict {r, g, b, a}");
    ObjDict *d = AS_DICT(v);
    NovaValue rv, gv, bv, av;
    if (!nova_table_get(&d->table, nova_string_copy("r", 1), &rv) ||
        !nova_table_get(&d->table, nova_string_copy("g", 1), &gv) ||
        !nova_table_get(&d->table, nova_string_copy("b", 1), &bv))
        nova_type_error(line, "color dict must have r, g, b keys");
    int a = 255;
    if (nova_table_get(&d->table, nova_string_copy("a", 1), &av))
        a = (int)AS_NUMBER(av);
    return (Color){ (unsigned char)AS_NUMBER(rv),
                    (unsigned char)AS_NUMBER(gv),
                    (unsigned char)AS_NUMBER(bv),
                    (unsigned char)a };
}

static NovaValue color_to_nova(int r, int g, int b, int a) {
    ObjDict *d = nova_dict_new();
    nova_table_set(&d->table, nova_string_copy("r", 1), NOVA_INT(r));
    nova_table_set(&d->table, nova_string_copy("g", 1), NOVA_INT(g));
    nova_table_set(&d->table, nova_string_copy("b", 1), NOVA_INT(b));
    nova_table_set(&d->table, nova_string_copy("a", 1), NOVA_INT(a));
    return NOVA_OBJ(d);
}

/* ── Vec3 conversion helpers ── */

static Vector3 nova_to_vec3(NovaValue v, int line) {
    if (!IS_DICT(v))
        nova_type_error(line, "expected a vec3 dict {x, y, z}");
    ObjDict *d = AS_DICT(v);
    NovaValue xv, yv, zv;
    if (!nova_table_get(&d->table, nova_string_copy("x", 1), &xv) ||
        !nova_table_get(&d->table, nova_string_copy("y", 1), &yv) ||
        !nova_table_get(&d->table, nova_string_copy("z", 1), &zv))
        nova_type_error(line, "vec3 dict must have x, y, z keys");
    return (Vector3){ (float)AS_NUMBER(xv), (float)AS_NUMBER(yv), (float)AS_NUMBER(zv) };
}

static NovaValue vec3_to_nova(float x, float y, float z) {
    ObjDict *d = nova_dict_new();
    nova_table_set(&d->table, nova_string_copy("x", 1), NOVA_FLOAT(x));
    nova_table_set(&d->table, nova_string_copy("y", 1), NOVA_FLOAT(y));
    nova_table_set(&d->table, nova_string_copy("z", 1), NOVA_FLOAT(z));
    return NOVA_OBJ(d);
}

/* ── Camera conversion helpers ── */

static Camera3D nova_to_camera(NovaValue v, int line) {
    if (!IS_DICT(v))
        nova_type_error(line, "expected a camera dict");
    ObjDict *d = AS_DICT(v);
    NovaValue px, py, pz, tx, ty, tz, ux, uy, uz, fovy, proj;
    if (!nova_table_get(&d->table, nova_string_copy("px", 2), &px) ||
        !nova_table_get(&d->table, nova_string_copy("py", 2), &py) ||
        !nova_table_get(&d->table, nova_string_copy("pz", 2), &pz) ||
        !nova_table_get(&d->table, nova_string_copy("tx", 2), &tx) ||
        !nova_table_get(&d->table, nova_string_copy("ty", 2), &ty) ||
        !nova_table_get(&d->table, nova_string_copy("tz", 2), &tz) ||
        !nova_table_get(&d->table, nova_string_copy("ux", 2), &ux) ||
        !nova_table_get(&d->table, nova_string_copy("uy", 2), &uy) ||
        !nova_table_get(&d->table, nova_string_copy("uz", 2), &uz) ||
        !nova_table_get(&d->table, nova_string_copy("fovy", 4), &fovy) ||
        !nova_table_get(&d->table, nova_string_copy("projection", 10), &proj))
        nova_type_error(line, "camera dict missing required keys");
    Camera3D cam = {0};
    cam.position = (Vector3){ (float)AS_NUMBER(px), (float)AS_NUMBER(py), (float)AS_NUMBER(pz) };
    cam.target   = (Vector3){ (float)AS_NUMBER(tx), (float)AS_NUMBER(ty), (float)AS_NUMBER(tz) };
    cam.up       = (Vector3){ (float)AS_NUMBER(ux), (float)AS_NUMBER(uy), (float)AS_NUMBER(uz) };
    cam.fovy     = (float)AS_NUMBER(fovy);
    cam.projection = (int)AS_NUMBER(proj);
    return cam;
}

/* ── Key string -> raylib key code ── */

static int key_from_string(const char *name, int line) {
    if (strcmp(name, "space") == 0) return KEY_SPACE;
    if (strcmp(name, "enter") == 0) return KEY_ENTER;
    if (strcmp(name, "escape") == 0) return KEY_ESCAPE;
    if (strcmp(name, "backspace") == 0) return KEY_BACKSPACE;
    if (strcmp(name, "tab") == 0) return KEY_TAB;
    if (strcmp(name, "up") == 0) return KEY_UP;
    if (strcmp(name, "down") == 0) return KEY_DOWN;
    if (strcmp(name, "left") == 0) return KEY_LEFT;
    if (strcmp(name, "right") == 0) return KEY_RIGHT;
    if (strcmp(name, "shift") == 0) return KEY_LEFT_SHIFT;
    if (strcmp(name, "ctrl") == 0) return KEY_LEFT_CONTROL;
    if (strcmp(name, "alt") == 0) return KEY_LEFT_ALT;
    /* Single letter a-z */
    if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z')
        return KEY_A + (name[0] - 'a');
    /* Digits 0-9 */
    if (strlen(name) == 1 && name[0] >= '0' && name[0] <= '9')
        return KEY_ZERO + (name[0] - '0');
    nova_runtime_error(line, "unknown key name: '%s'", name);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
   Window & Lifecycle
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_init(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int w = (int)AS_NUMBER(argv[0]);
    int h = (int)AS_NUMBER(argv[1]);
    if (!IS_STRING(argv[2]))
        nova_type_error(line, "init() expects a string title");
    InitWindow(w, h, AS_CSTRING(argv[2]));
    return NOVA_NONE();
}

static NovaValue game_close(Interpreter *interp, int argc, NovaValue *argv, int line) {
    CloseWindow();
    texture_count = 0;
    sound_count = 0;
    music_count = 0;
    model_count = 0;
    return NOVA_NONE();
}

static NovaValue game_is_running(Interpreter *interp, int argc, NovaValue *argv, int line) {
    return NOVA_BOOL(!WindowShouldClose());
}

static NovaValue game_set_fps(Interpreter *interp, int argc, NovaValue *argv, int line) {
    SetTargetFPS((int)AS_NUMBER(argv[0]));
    return NOVA_NONE();
}

static NovaValue game_get_fps(Interpreter *interp, int argc, NovaValue *argv, int line) {
    return NOVA_INT(GetFPS());
}

static NovaValue game_delta_time(Interpreter *interp, int argc, NovaValue *argv, int line) {
    return NOVA_FLOAT(GetFrameTime());
}

/* ══════════════════════════════════════════════════════════════════
   Drawing Control
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_begin_drawing(Interpreter *interp, int argc, NovaValue *argv, int line) {
    BeginDrawing();
    return NOVA_NONE();
}

static NovaValue game_end_drawing(Interpreter *interp, int argc, NovaValue *argv, int line) {
    EndDrawing();
    return NOVA_NONE();
}

/* ══════════════════════════════════════════════════════════════════
   Background
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_clear(Interpreter *interp, int argc, NovaValue *argv, int line) {
    ClearBackground(nova_to_color(argv[0], line));
    return NOVA_NONE();
}

/* ══════════════════════════════════════════════════════════════════
   Shape Drawing
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_draw_rect(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int x = (int)AS_NUMBER(argv[0]);
    int y = (int)AS_NUMBER(argv[1]);
    int w = (int)AS_NUMBER(argv[2]);
    int h = (int)AS_NUMBER(argv[3]);
    DrawRectangle(x, y, w, h, nova_to_color(argv[4], line));
    return NOVA_NONE();
}

static NovaValue game_draw_rect_lines(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int x = (int)AS_NUMBER(argv[0]);
    int y = (int)AS_NUMBER(argv[1]);
    int w = (int)AS_NUMBER(argv[2]);
    int h = (int)AS_NUMBER(argv[3]);
    DrawRectangleLines(x, y, w, h, nova_to_color(argv[4], line));
    return NOVA_NONE();
}

static NovaValue game_draw_circle(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int cx = (int)AS_NUMBER(argv[0]);
    int cy = (int)AS_NUMBER(argv[1]);
    float r = (float)AS_NUMBER(argv[2]);
    DrawCircle(cx, cy, r, nova_to_color(argv[3], line));
    return NOVA_NONE();
}

static NovaValue game_draw_line(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int x1 = (int)AS_NUMBER(argv[0]);
    int y1 = (int)AS_NUMBER(argv[1]);
    int x2 = (int)AS_NUMBER(argv[2]);
    int y2 = (int)AS_NUMBER(argv[3]);
    DrawLine(x1, y1, x2, y2, nova_to_color(argv[4], line));
    return NOVA_NONE();
}

static NovaValue game_draw_triangle(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector2 v1 = { (float)AS_NUMBER(argv[0]), (float)AS_NUMBER(argv[1]) };
    Vector2 v2 = { (float)AS_NUMBER(argv[2]), (float)AS_NUMBER(argv[3]) };
    Vector2 v3 = { (float)AS_NUMBER(argv[4]), (float)AS_NUMBER(argv[5]) };
    DrawTriangle(v1, v2, v3, nova_to_color(argv[6], line));
    return NOVA_NONE();
}

static NovaValue game_draw_text(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "draw_text() expects a string as first argument");
    int x = (int)AS_NUMBER(argv[1]);
    int y = (int)AS_NUMBER(argv[2]);
    int size = (int)AS_NUMBER(argv[3]);
    DrawText(AS_CSTRING(argv[0]), x, y, size, nova_to_color(argv[4], line));
    return NOVA_NONE();
}

/* ══════════════════════════════════════════════════════════════════
   Input — Keyboard
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_key_pressed(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "key_pressed() expects a key name string");
    return NOVA_BOOL(IsKeyPressed(key_from_string(AS_CSTRING(argv[0]), line)));
}

static NovaValue game_key_down(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "key_down() expects a key name string");
    return NOVA_BOOL(IsKeyDown(key_from_string(AS_CSTRING(argv[0]), line)));
}

static NovaValue game_key_released(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "key_released() expects a key name string");
    return NOVA_BOOL(IsKeyReleased(key_from_string(AS_CSTRING(argv[0]), line)));
}

/* ══════════════════════════════════════════════════════════════════
   Input — Mouse
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_mouse_x(Interpreter *interp, int argc, NovaValue *argv, int line) {
    return NOVA_INT(GetMouseX());
}

static NovaValue game_mouse_y(Interpreter *interp, int argc, NovaValue *argv, int line) {
    return NOVA_INT(GetMouseY());
}

static NovaValue game_mouse_pressed(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int btn = (int)AS_NUMBER(argv[0]);
    return NOVA_BOOL(IsMouseButtonPressed(btn));
}

static NovaValue game_mouse_down(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int btn = (int)AS_NUMBER(argv[0]);
    return NOVA_BOOL(IsMouseButtonDown(btn));
}

static NovaValue game_mouse_released(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int btn = (int)AS_NUMBER(argv[0]);
    return NOVA_BOOL(IsMouseButtonReleased(btn));
}

/* ══════════════════════════════════════════════════════════════════
   Textures
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_load_image(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "load_image() expects a file path string");
    if (texture_count >= 256)
        nova_runtime_error(line, "load_image() texture limit reached (256)");
    textures[texture_count] = LoadTexture(AS_CSTRING(argv[0]));
    return NOVA_INT(texture_count++);
}

static NovaValue game_draw_image(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= texture_count)
        nova_runtime_error(line, "draw_image() invalid texture handle: %d", handle);
    int x = (int)AS_NUMBER(argv[1]);
    int y = (int)AS_NUMBER(argv[2]);
    DrawTexture(textures[handle], x, y, WHITE);
    return NOVA_NONE();
}

static NovaValue game_draw_image_scaled(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= texture_count)
        nova_runtime_error(line, "draw_image_scaled() invalid texture handle: %d", handle);
    float x = (float)AS_NUMBER(argv[1]);
    float y = (float)AS_NUMBER(argv[2]);
    float scale = (float)AS_NUMBER(argv[3]);
    DrawTextureEx(textures[handle], (Vector2){x, y}, 0.0f, scale, WHITE);
    return NOVA_NONE();
}

static NovaValue game_unload_image(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= texture_count)
        nova_runtime_error(line, "unload_image() invalid texture handle: %d", handle);
    UnloadTexture(textures[handle]);
    return NOVA_NONE();
}

/* ══════════════════════════════════════════════════════════════════
   Audio
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_init_audio(Interpreter *interp, int argc, NovaValue *argv, int line) {
    InitAudioDevice();
    return NOVA_NONE();
}

static NovaValue game_load_sound(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "load_sound() expects a file path string");
    if (sound_count >= 256)
        nova_runtime_error(line, "load_sound() sound limit reached (256)");
    sounds[sound_count] = LoadSound(AS_CSTRING(argv[0]));
    return NOVA_INT(sound_count++);
}

static NovaValue game_play_sound(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= sound_count)
        nova_runtime_error(line, "play_sound() invalid sound handle: %d", handle);
    PlaySound(sounds[handle]);
    return NOVA_NONE();
}

static NovaValue game_load_music(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "load_music() expects a file path string");
    if (music_count >= 64)
        nova_runtime_error(line, "load_music() music limit reached (64)");
    musics[music_count] = LoadMusicStream(AS_CSTRING(argv[0]));
    return NOVA_INT(music_count++);
}

static NovaValue game_play_music(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= music_count)
        nova_runtime_error(line, "play_music() invalid music handle: %d", handle);
    PlayMusicStream(musics[handle]);
    return NOVA_NONE();
}

static NovaValue game_update_music(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= music_count)
        nova_runtime_error(line, "update_music() invalid music handle: %d", handle);
    UpdateMusicStream(musics[handle]);
    return NOVA_NONE();
}

/* ══════════════════════════════════════════════════════════════════
   Collision Detection
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_check_collision_rects(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Rectangle r1 = { (float)AS_NUMBER(argv[0]), (float)AS_NUMBER(argv[1]),
                     (float)AS_NUMBER(argv[2]), (float)AS_NUMBER(argv[3]) };
    Rectangle r2 = { (float)AS_NUMBER(argv[4]), (float)AS_NUMBER(argv[5]),
                     (float)AS_NUMBER(argv[6]), (float)AS_NUMBER(argv[7]) };
    return NOVA_BOOL(CheckCollisionRecs(r1, r2));
}

static NovaValue game_check_collision_circles(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector2 c1 = { (float)AS_NUMBER(argv[0]), (float)AS_NUMBER(argv[1]) };
    float r1 = (float)AS_NUMBER(argv[2]);
    Vector2 c2 = { (float)AS_NUMBER(argv[3]), (float)AS_NUMBER(argv[4]) };
    float r2 = (float)AS_NUMBER(argv[5]);
    return NOVA_BOOL(CheckCollisionCircles(c1, r1, c2, r2));
}

/* ══════════════════════════════════════════════════════════════════
   3D — Vector & Camera Helpers
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_vec3(Interpreter *interp, int argc, NovaValue *argv, int line) {
    return vec3_to_nova((float)AS_NUMBER(argv[0]),
                        (float)AS_NUMBER(argv[1]),
                        (float)AS_NUMBER(argv[2]));
}

static NovaValue game_camera(Interpreter *interp, int argc, NovaValue *argv, int line) {
    ObjDict *d = nova_dict_new();
    nova_table_set(&d->table, nova_string_copy("px", 2), argv[0]);
    nova_table_set(&d->table, nova_string_copy("py", 2), argv[1]);
    nova_table_set(&d->table, nova_string_copy("pz", 2), argv[2]);
    nova_table_set(&d->table, nova_string_copy("tx", 2), argv[3]);
    nova_table_set(&d->table, nova_string_copy("ty", 2), argv[4]);
    nova_table_set(&d->table, nova_string_copy("tz", 2), argv[5]);
    nova_table_set(&d->table, nova_string_copy("ux", 2), NOVA_FLOAT(0));
    nova_table_set(&d->table, nova_string_copy("uy", 2), NOVA_FLOAT(1));
    nova_table_set(&d->table, nova_string_copy("uz", 2), NOVA_FLOAT(0));
    nova_table_set(&d->table, nova_string_copy("fovy", 4), argv[6]);
    nova_table_set(&d->table, nova_string_copy("projection", 10), NOVA_INT(CAMERA_PERSPECTIVE));
    return NOVA_OBJ(d);
}

static NovaValue game_begin_3d(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Camera3D cam = nova_to_camera(argv[0], line);
    BeginMode3D(cam);
    return NOVA_NONE();
}

static NovaValue game_end_3d(Interpreter *interp, int argc, NovaValue *argv, int line) {
    EndMode3D();
    return NOVA_NONE();
}

/* ══════════════════════════════════════════════════════════════════
   3D — Shape Drawing
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_draw_cube(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 pos = nova_to_vec3(argv[0], line);
    float w = (float)AS_NUMBER(argv[1]);
    float h = (float)AS_NUMBER(argv[2]);
    float l = (float)AS_NUMBER(argv[3]);
    DrawCube(pos, w, h, l, nova_to_color(argv[4], line));
    return NOVA_NONE();
}

static NovaValue game_draw_cube_wires(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 pos = nova_to_vec3(argv[0], line);
    float w = (float)AS_NUMBER(argv[1]);
    float h = (float)AS_NUMBER(argv[2]);
    float l = (float)AS_NUMBER(argv[3]);
    DrawCubeWires(pos, w, h, l, nova_to_color(argv[4], line));
    return NOVA_NONE();
}

static NovaValue game_draw_sphere(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 pos = nova_to_vec3(argv[0], line);
    float radius = (float)AS_NUMBER(argv[1]);
    DrawSphere(pos, radius, nova_to_color(argv[2], line));
    return NOVA_NONE();
}

static NovaValue game_draw_sphere_wires(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 pos = nova_to_vec3(argv[0], line);
    float radius = (float)AS_NUMBER(argv[1]);
    int rings = (int)AS_NUMBER(argv[2]);
    int slices = (int)AS_NUMBER(argv[3]);
    DrawSphereWires(pos, radius, rings, slices, nova_to_color(argv[4], line));
    return NOVA_NONE();
}

static NovaValue game_draw_cylinder(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 pos = nova_to_vec3(argv[0], line);
    float rtop = (float)AS_NUMBER(argv[1]);
    float rbottom = (float)AS_NUMBER(argv[2]);
    float height = (float)AS_NUMBER(argv[3]);
    int slices = (int)AS_NUMBER(argv[4]);
    DrawCylinder(pos, rtop, rbottom, height, slices, nova_to_color(argv[5], line));
    return NOVA_NONE();
}

static NovaValue game_draw_cylinder_wires(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 pos = nova_to_vec3(argv[0], line);
    float rtop = (float)AS_NUMBER(argv[1]);
    float rbottom = (float)AS_NUMBER(argv[2]);
    float height = (float)AS_NUMBER(argv[3]);
    int slices = (int)AS_NUMBER(argv[4]);
    DrawCylinderWires(pos, rtop, rbottom, height, slices, nova_to_color(argv[5], line));
    return NOVA_NONE();
}

static NovaValue game_draw_plane(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 pos = nova_to_vec3(argv[0], line);
    float w = (float)AS_NUMBER(argv[1]);
    float h = (float)AS_NUMBER(argv[2]);
    DrawPlane(pos, (Vector2){w, h}, nova_to_color(argv[3], line));
    return NOVA_NONE();
}

static NovaValue game_draw_grid(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int slices = (int)AS_NUMBER(argv[0]);
    float spacing = (float)AS_NUMBER(argv[1]);
    DrawGrid(slices, spacing);
    return NOVA_NONE();
}

static NovaValue game_draw_line_3d(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 start = nova_to_vec3(argv[0], line);
    Vector3 end = nova_to_vec3(argv[1], line);
    DrawLine3D(start, end, nova_to_color(argv[2], line));
    return NOVA_NONE();
}

/* ══════════════════════════════════════════════════════════════════
   3D — Models
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_load_model(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "load_model() expects a file path string");
    if (model_count >= 128)
        nova_runtime_error(line, "load_model() model limit reached (128)");
    models[model_count] = LoadModel(AS_CSTRING(argv[0]));
    return NOVA_INT(model_count++);
}

static NovaValue game_unload_model(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= model_count)
        nova_runtime_error(line, "unload_model() invalid model handle: %d", handle);
    UnloadModel(models[handle]);
    return NOVA_NONE();
}

static NovaValue game_draw_model(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= model_count)
        nova_runtime_error(line, "draw_model() invalid model handle: %d", handle);
    Vector3 pos = nova_to_vec3(argv[1], line);
    float scale = (float)AS_NUMBER(argv[2]);
    DrawModel(models[handle], pos, scale, nova_to_color(argv[3], line));
    return NOVA_NONE();
}

static NovaValue game_draw_model_ex(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int handle = (int)AS_NUMBER(argv[0]);
    if (handle < 0 || handle >= model_count)
        nova_runtime_error(line, "draw_model_ex() invalid model handle: %d", handle);
    Vector3 pos = nova_to_vec3(argv[1], line);
    Vector3 rot_axis = nova_to_vec3(argv[2], line);
    float rot_angle = (float)AS_NUMBER(argv[3]);
    Vector3 scale = nova_to_vec3(argv[4], line);
    DrawModelEx(models[handle], pos, rot_axis, rot_angle, scale, nova_to_color(argv[5], line));
    return NOVA_NONE();
}

/* ══════════════════════════════════════════════════════════════════
   3D — Collision Detection
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_check_collision_spheres_3d(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 c1 = nova_to_vec3(argv[0], line);
    float r1 = (float)AS_NUMBER(argv[1]);
    Vector3 c2 = nova_to_vec3(argv[2], line);
    float r2 = (float)AS_NUMBER(argv[3]);
    return NOVA_BOOL(CheckCollisionSpheres(c1, r1, c2, r2));
}

static NovaValue game_check_collision_boxes_3d(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 min1 = nova_to_vec3(argv[0], line);
    Vector3 max1 = nova_to_vec3(argv[1], line);
    Vector3 min2 = nova_to_vec3(argv[2], line);
    Vector3 max2 = nova_to_vec3(argv[3], line);
    BoundingBox b1 = { min1, max1 };
    BoundingBox b2 = { min2, max2 };
    return NOVA_BOOL(CheckCollisionBoxes(b1, b2));
}

static NovaValue game_check_collision_box_sphere(Interpreter *interp, int argc, NovaValue *argv, int line) {
    Vector3 bmin = nova_to_vec3(argv[0], line);
    Vector3 bmax = nova_to_vec3(argv[1], line);
    Vector3 center = nova_to_vec3(argv[2], line);
    float radius = (float)AS_NUMBER(argv[3]);
    BoundingBox box = { bmin, bmax };
    return NOVA_BOOL(CheckCollisionBoxSphere(box, center, radius));
}

/* ══════════════════════════════════════════════════════════════════
   Color Helper
   ══════════════════════════════════════════════════════════════════ */

static NovaValue game_color(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int r = (int)AS_NUMBER(argv[0]);
    int g = (int)AS_NUMBER(argv[1]);
    int b = (int)AS_NUMBER(argv[2]);
    int a = (argc >= 4) ? (int)AS_NUMBER(argv[3]) : 255;
    return color_to_nova(r, g, b, a);
}

/* ══════════════════════════════════════════════════════════════════
   Module Init
   ══════════════════════════════════════════════════════════════════ */

void nova_module_init(Interpreter *interp, Environment *env) {
    /* Window & Lifecycle */
    nova_env_define(env, "init",          NOVA_OBJ(nova_builtin_new("init",          game_init,          3)));
    nova_env_define(env, "close",         NOVA_OBJ(nova_builtin_new("close",         game_close,         0)));
    nova_env_define(env, "is_running",    NOVA_OBJ(nova_builtin_new("is_running",    game_is_running,    0)));
    nova_env_define(env, "set_fps",       NOVA_OBJ(nova_builtin_new("set_fps",       game_set_fps,       1)));
    nova_env_define(env, "get_fps",       NOVA_OBJ(nova_builtin_new("get_fps",       game_get_fps,       0)));
    nova_env_define(env, "delta_time",    NOVA_OBJ(nova_builtin_new("delta_time",    game_delta_time,    0)));

    /* Drawing Control */
    nova_env_define(env, "begin_drawing", NOVA_OBJ(nova_builtin_new("begin_drawing", game_begin_drawing, 0)));
    nova_env_define(env, "end_drawing",   NOVA_OBJ(nova_builtin_new("end_drawing",   game_end_drawing,   0)));

    /* Background */
    nova_env_define(env, "clear",         NOVA_OBJ(nova_builtin_new("clear",         game_clear,         1)));

    /* Shape Drawing */
    nova_env_define(env, "draw_rect",       NOVA_OBJ(nova_builtin_new("draw_rect",       game_draw_rect,       5)));
    nova_env_define(env, "draw_rect_lines", NOVA_OBJ(nova_builtin_new("draw_rect_lines", game_draw_rect_lines, 5)));
    nova_env_define(env, "draw_circle",     NOVA_OBJ(nova_builtin_new("draw_circle",     game_draw_circle,     4)));
    nova_env_define(env, "draw_line",       NOVA_OBJ(nova_builtin_new("draw_line",       game_draw_line,       5)));
    nova_env_define(env, "draw_triangle",   NOVA_OBJ(nova_builtin_new("draw_triangle",   game_draw_triangle,   7)));
    nova_env_define(env, "draw_text",       NOVA_OBJ(nova_builtin_new("draw_text",       game_draw_text,       5)));

    /* Input — Keyboard */
    nova_env_define(env, "key_pressed",   NOVA_OBJ(nova_builtin_new("key_pressed",   game_key_pressed,   1)));
    nova_env_define(env, "key_down",      NOVA_OBJ(nova_builtin_new("key_down",      game_key_down,      1)));
    nova_env_define(env, "key_released",  NOVA_OBJ(nova_builtin_new("key_released",  game_key_released,  1)));

    /* Input — Mouse */
    nova_env_define(env, "mouse_x",        NOVA_OBJ(nova_builtin_new("mouse_x",        game_mouse_x,        0)));
    nova_env_define(env, "mouse_y",        NOVA_OBJ(nova_builtin_new("mouse_y",        game_mouse_y,        0)));
    nova_env_define(env, "mouse_pressed",  NOVA_OBJ(nova_builtin_new("mouse_pressed",  game_mouse_pressed,  1)));
    nova_env_define(env, "mouse_down",     NOVA_OBJ(nova_builtin_new("mouse_down",     game_mouse_down,     1)));
    nova_env_define(env, "mouse_released", NOVA_OBJ(nova_builtin_new("mouse_released", game_mouse_released, 1)));

    /* Textures */
    nova_env_define(env, "load_image",        NOVA_OBJ(nova_builtin_new("load_image",        game_load_image,        1)));
    nova_env_define(env, "draw_image",        NOVA_OBJ(nova_builtin_new("draw_image",        game_draw_image,        3)));
    nova_env_define(env, "draw_image_scaled", NOVA_OBJ(nova_builtin_new("draw_image_scaled", game_draw_image_scaled, 4)));
    nova_env_define(env, "unload_image",      NOVA_OBJ(nova_builtin_new("unload_image",      game_unload_image,      1)));

    /* Audio */
    nova_env_define(env, "init_audio",    NOVA_OBJ(nova_builtin_new("init_audio",    game_init_audio,    0)));
    nova_env_define(env, "load_sound",    NOVA_OBJ(nova_builtin_new("load_sound",    game_load_sound,    1)));
    nova_env_define(env, "play_sound",    NOVA_OBJ(nova_builtin_new("play_sound",    game_play_sound,    1)));
    nova_env_define(env, "load_music",    NOVA_OBJ(nova_builtin_new("load_music",    game_load_music,    1)));
    nova_env_define(env, "play_music",    NOVA_OBJ(nova_builtin_new("play_music",    game_play_music,    1)));
    nova_env_define(env, "update_music",  NOVA_OBJ(nova_builtin_new("update_music",  game_update_music,  1)));

    /* Collision Detection */
    nova_env_define(env, "check_collision_rects",   NOVA_OBJ(nova_builtin_new("check_collision_rects",   game_check_collision_rects,   8)));
    nova_env_define(env, "check_collision_circles", NOVA_OBJ(nova_builtin_new("check_collision_circles", game_check_collision_circles, 6)));

    /* 3D — Vector & Camera */
    nova_env_define(env, "vec3",      NOVA_OBJ(nova_builtin_new("vec3",      game_vec3,      3)));
    nova_env_define(env, "camera",    NOVA_OBJ(nova_builtin_new("camera",    game_camera,    7)));
    nova_env_define(env, "begin_3d",  NOVA_OBJ(nova_builtin_new("begin_3d",  game_begin_3d,  1)));
    nova_env_define(env, "end_3d",    NOVA_OBJ(nova_builtin_new("end_3d",    game_end_3d,    0)));

    /* 3D — Shapes */
    nova_env_define(env, "draw_cube",           NOVA_OBJ(nova_builtin_new("draw_cube",           game_draw_cube,           5)));
    nova_env_define(env, "draw_cube_wires",     NOVA_OBJ(nova_builtin_new("draw_cube_wires",     game_draw_cube_wires,     5)));
    nova_env_define(env, "draw_sphere",         NOVA_OBJ(nova_builtin_new("draw_sphere",         game_draw_sphere,         3)));
    nova_env_define(env, "draw_sphere_wires",   NOVA_OBJ(nova_builtin_new("draw_sphere_wires",   game_draw_sphere_wires,   5)));
    nova_env_define(env, "draw_cylinder",       NOVA_OBJ(nova_builtin_new("draw_cylinder",       game_draw_cylinder,       6)));
    nova_env_define(env, "draw_cylinder_wires", NOVA_OBJ(nova_builtin_new("draw_cylinder_wires", game_draw_cylinder_wires, 6)));
    nova_env_define(env, "draw_plane",          NOVA_OBJ(nova_builtin_new("draw_plane",          game_draw_plane,          4)));
    nova_env_define(env, "draw_grid",           NOVA_OBJ(nova_builtin_new("draw_grid",           game_draw_grid,           2)));
    nova_env_define(env, "draw_line_3d",        NOVA_OBJ(nova_builtin_new("draw_line_3d",        game_draw_line_3d,        3)));

    /* 3D — Models */
    nova_env_define(env, "load_model",     NOVA_OBJ(nova_builtin_new("load_model",     game_load_model,     1)));
    nova_env_define(env, "unload_model",   NOVA_OBJ(nova_builtin_new("unload_model",   game_unload_model,   1)));
    nova_env_define(env, "draw_model",     NOVA_OBJ(nova_builtin_new("draw_model",     game_draw_model,     4)));
    nova_env_define(env, "draw_model_ex",  NOVA_OBJ(nova_builtin_new("draw_model_ex",  game_draw_model_ex,  6)));

    /* 3D — Collision Detection */
    nova_env_define(env, "check_collision_spheres_3d",  NOVA_OBJ(nova_builtin_new("check_collision_spheres_3d",  game_check_collision_spheres_3d,  4)));
    nova_env_define(env, "check_collision_boxes_3d",    NOVA_OBJ(nova_builtin_new("check_collision_boxes_3d",    game_check_collision_boxes_3d,    4)));
    nova_env_define(env, "check_collision_box_sphere",  NOVA_OBJ(nova_builtin_new("check_collision_box_sphere",  game_check_collision_box_sphere,  4)));

    /* Color Helper (variadic: 3 or 4 args) */
    nova_env_define(env, "color", NOVA_OBJ(nova_builtin_new("color", game_color, -1)));

    /* Color Constants */
    nova_env_define(env, "BLACK",   color_to_nova(0,   0,   0,   255));
    nova_env_define(env, "WHITE",   color_to_nova(255, 255, 255, 255));
    nova_env_define(env, "RED",     color_to_nova(230, 41,  55,  255));
    nova_env_define(env, "GREEN",   color_to_nova(0,   228, 48,  255));
    nova_env_define(env, "BLUE",    color_to_nova(0,   121, 241, 255));
    nova_env_define(env, "YELLOW",  color_to_nova(253, 249, 0,   255));
    nova_env_define(env, "ORANGE",  color_to_nova(255, 161, 0,   255));
    nova_env_define(env, "PINK",    color_to_nova(255, 109, 194, 255));
    nova_env_define(env, "PURPLE",  color_to_nova(200, 122, 255, 255));
    nova_env_define(env, "SKYBLUE", color_to_nova(102, 191, 255, 255));
    nova_env_define(env, "BROWN",   color_to_nova(127, 106, 79,  255));
    nova_env_define(env, "GRAY",    color_to_nova(130, 130, 130, 255));

    /* Camera Constants */
    nova_env_define(env, "CAMERA_PERSPECTIVE",   NOVA_INT(CAMERA_PERSPECTIVE));
    nova_env_define(env, "CAMERA_FREE",          NOVA_INT(CAMERA_FREE));
    nova_env_define(env, "CAMERA_ORBITAL",       NOVA_INT(CAMERA_ORBITAL));
    nova_env_define(env, "CAMERA_FIRST_PERSON",  NOVA_INT(CAMERA_FIRST_PERSON));
    nova_env_define(env, "CAMERA_THIRD_PERSON",  NOVA_INT(CAMERA_THIRD_PERSON));
}

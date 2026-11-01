// `zaro export android`: turns a scene into a Gradle project around the Android runtime (D-022).
#include "app/sprite_layer.h"
#include "app/tools.h"

#include "core/diag.h"
#include "core/log.h"
#include "core/path.h"
#include "platform/platform.h"
#include "scene/build_extension.h"
#include "scene/scene.h"
#include "scene/sprite_sheet.h"
#include "sim/sprite_components.h"
#include "ui/widgets.h"
#include "zaro/zaro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

constexpr uint32_t kMaxFiles = 1024;
constexpr size_t kPath = 512;
constexpr size_t kLongPath = 2048;  // a directory joined with a kPath-sized relative path

struct PathList {
    char (*items)[kPath] = nullptr;
    uint32_t count = 0;
};

bool add_unique(PathList& list, const char* path) {
    for (uint32_t i = 0; i < list.count; ++i) {
        if (strcmp(list.items[i], path) == 0) return true;
    }
    if (list.count == kMaxFiles) return false;
    snprintf(list.items[list.count++], kPath, "%s", path);
    return true;
}

// Directory part of `path` including the trailing '/', or "" for a bare file name.
size_t dir_length(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? (size_t)(slash - path) + 1 : 0;
}

bool starts_with(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

bool ends_with(const char* s, const char* suffix) {
    const size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

// Letters, digits and '_' only, lowercase, starting with a letter: one Java package segment.
void package_segment(const char* name, char* out, size_t size) {
    size_t o = 0;
    for (const char* c = name; *c && o + 1 < size; ++c) {
        char ch = *c;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) ch = '_';
        if (o == 0 && !(ch >= 'a' && ch <= 'z')) out[o++] = 'g';
        if (o + 1 < size) out[o++] = ch;
    }
    if (o == 0) out[o++] = 'g';
    out[o] = 0;
}

// A version name goes into a Groovy string literal: keep it to characters that need no escaping.
bool valid_version_name(const char* s) {
    const size_t n = strlen(s);
    if (n == 0 || n > 64) return false;
    for (const char* c = s; *c; ++c) {
        const char ch = *c;
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == ' ' || ch == '.' || ch == '_' || ch == '+' || ch == '-';
        if (!ok) return false;
    }
    return true;
}

bool valid_package(const char* id) {
    int segments = 0;
    const char* p = id;
    while (*p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return false;
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') ++p;
        ++segments;
        if (*p == '.') ++p;
        else if (*p) return false;
    }
    return segments >= 2 && !ends_with(id, ".");
}

// Text a label or a Gradle project name can hold without escaping.
void display_name(const char* name, char* out, size_t size) {
    size_t o = 0;
    for (const char* c = name; *c && o + 1 < size; ++c) {
        const char ch = *c;
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == ' ' || ch == '-' || ch == '_' || ch == '.';
        out[o++] = ok ? ch : '_';
    }
    out[o] = 0;
    if (o == 0) snprintf(out, size, "zaro");
}

char* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = (char*)malloc((size_t)(n > 0 ? n : 0) + 1);
    const size_t got = fread(data, 1, (size_t)(n > 0 ? n : 0), f);
    fclose(f);
    data[got] = 0;
    *size = got;
    return data;
}

bool write_file(const char* path, const char* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

struct Substitution {
    const char* key;
    const char* value;
};

struct Export {
    const char* runtime;      // normalized, no trailing '/'
    const char* out;          // normalized, no trailing '/'
    const Substitution* subs;
    uint32_t sub_count;
    PathList assets;          // paths below app/src/main/assets, for zaro_assets.txt
    PathList java;            // project paths of every .java or .kt file written so far
    bool ok = true;
};

constexpr char kJavaRoot[] = "app/src/main/java/";

bool make_parent(const char* path) {
    char dir[kLongPath];
    snprintf(dir, sizeof(dir), "%s", path);
    char* slash = strrchr(dir, '/');
    if (!slash) return true;
    *slash = 0;
    return platform::create_directories(dir);
}

// Copies one file, filling in @ZARO_...@ placeholders in the template's text files.
bool copy_with_substitutions(const Export& e, const char* from, const char* to) {
    if (!make_parent(to)) return false;
    if (!ends_with(from, ".gradle") && !ends_with(from, ".xml") && !ends_with(from, ".properties")) {
        return platform::copy_file(from, to);
    }
    size_t size = 0;
    char* text = read_file(from, &size);
    if (!text) return false;
    for (uint32_t i = 0; i < e.sub_count; ++i) {
        const size_t klen = strlen(e.subs[i].key), vlen = strlen(e.subs[i].value);
        for (char* hit = strstr(text, e.subs[i].key); hit; hit = strstr(hit, e.subs[i].key)) {
            const size_t at = (size_t)(hit - text);
            char* grown = (char*)malloc(size - klen + vlen + 1);
            memcpy(grown, text, at);
            memcpy(grown + at, e.subs[i].value, vlen);
            memcpy(grown + at + vlen, text + at + klen, size - at - klen + 1);
            free(text);
            text = grown;
            size = size - klen + vlen;
            hit = text + at + vlen;
        }
    }
    const bool ok = write_file(to, text, size);
    free(text);
    return ok;
}

void copy_runtime_file(const char* path, void* user) {
    Export& e = *(Export*)user;
    const char* rel = path + strlen(e.runtime) + 1;
    char to[kLongPath];
    snprintf(to, sizeof(to), "%s/%s", e.out, rel);
    if (!copy_with_substitutions(e, path, to)) {
        log_error(kErrIo, "%s: cannot write", to);
        e.ok = false;
    }
    static const char kAssets[] = "app/src/main/assets/";
    if (starts_with(rel, kAssets) && !add_unique(e.assets, rel + sizeof(kAssets) - 1)) e.ok = false;
    if (starts_with(rel, kJavaRoot) && !add_unique(e.java, rel)) e.ok = false;
}

void strip_trailing_slash(char* path);

// --- Build extensions (D-026) ----------------------------------------------------------------------

void append(char** text, const char* piece) {
    const size_t old = *text ? strlen(*text) : 0, add = strlen(piece);
    char* grown = (char*)realloc(*text, old + add + 1);
    memcpy(grown + old, piece, add + 1);
    *text = grown;
}

// What the scene's build extensions add: Gradle dependency lines, XML for <manifest> and for
// <application>, and source directories copied once the runtime is in place.
struct ExtensionText {
    char* dependencies = nullptr;
    char* manifest = nullptr;
    char* application = nullptr;
    scene::BuildExtension* items[scene::kMaxBuildExtensions] = {};
    uint32_t count = 0;
};

bool gather_extensions(const scene::Scene* s, ExtensionText* x) {
    append(&x->dependencies, "");
    append(&x->manifest, "");
    append(&x->application, "");
    bool ok = true;
    for (uint32_t i = 0; i < s->build_extension_count; ++i) {
        DiagList* diags = new DiagList();
        scene::BuildExtension* b = scene::load_build_extension(s->build_extensions[i], diags);
        diag_print_text(diags, stderr);
        delete diags;
        if (!b) {
            ok = false;
            continue;
        }
        char line[scene::kExtensionItemCapacity + 64];
        for (uint32_t k = 0; k < b->dependency_count; ++k) {
            snprintf(line, sizeof(line), "    implementation '%s'\n", b->dependencies[k]);
            append(&x->dependencies, line);
        }
        for (uint32_t k = 0; k < b->permission_count; ++k) {
            snprintf(line, sizeof(line), "    <uses-permission android:name=\"%s\" />\n", b->permissions[k]);
            append(&x->manifest, line);
        }
        if (b->manifest_xml) {
            append(&x->manifest, b->manifest_xml);
            append(&x->manifest, "\n");
        }
        if (b->application_xml) {
            append(&x->application, b->application_xml);
            append(&x->application, "\n");
        }
        x->items[x->count++] = b;
    }
    return ok;
}

struct SourceCopy {
    Export* e;
    const scene::BuildExtension* extension;
    size_t root_length;  // of the extension's sources directory, with its '/'
};

// Copies one .java or .kt file under app/src/main/java; never replaces a file of the runtime or of
// another extension.
void copy_extension_source(const char* path, void* user) {
    SourceCopy& c = *(SourceCopy*)user;
    if (!ends_with(path, ".java") && !ends_with(path, ".kt")) return;
    char rel[kPath + 32], to[kLongPath];
    snprintf(rel, sizeof(rel), "%s%s", kJavaRoot, path + c.root_length);
    for (uint32_t i = 0; i < c.e->java.count; ++i) {
        if (strcmp(c.e->java.items[i], rel) == 0) {
            log_error(kErrExport, "%s:%d: %s would replace %s, which the runtime or another build extension "
                      "already provides", c.extension->path, c.extension->sources_line, path, rel);
            c.e->ok = false;
            return;
        }
    }
    snprintf(to, sizeof(to), "%s/%s", c.e->out, rel);
    if (strlen(rel) >= kPath || !add_unique(c.e->java, rel) || !make_parent(to) || !platform::copy_file(path, to)) {
        log_error(kErrIo, "%s: cannot copy to %s", path, to);
        c.e->ok = false;
    }
}

void copy_extension_sources(Export& e, const ExtensionText& x) {
    for (uint32_t i = 0; i < x.count; ++i) {
        const scene::BuildExtension* b = x.items[i];
        if (!b->sources[0]) continue;
        char root[kPath];
        snprintf(root, sizeof(root), "%s", b->sources);
        strip_trailing_slash(root);
        SourceCopy c{&e, b, strlen(root) + 1};
        if (platform::file_mtime(root) == 0 || !platform::find_files(root, "", copy_extension_source, &c)) {
            log_error(kErrExport, "%s:%d: sources directory '%s' does not exist", b->path, b->sources_line, root);
            e.ok = false;
        }
    }
}

void free_extensions(ExtensionText* x) {
    for (uint32_t i = 0; i < x->count; ++i) scene::free_build_extension(x->items[i]);
    free(x->dependencies);
    free(x->manifest);
    free(x->application);
}

// A sprite sheet and its image. An SVG sheet is cooked first (D-028) and packs the cooked PNG; the
// APK never holds the SVG files.
void add_sheet_files(PathList& files, const char* doc_path, const char* sheet_rel, bool* ok) {
    char path[kPath];
    path_join_relative(doc_path, sheet_rel, path, sizeof(path));
    *ok &= add_unique(files, path);
    DiagList* diags = new DiagList();
    if (scene::SpriteSheet* sheet = scene::load_sprite_sheet(path, diags)) {
        bool cooked = false;
        if (ensure_sheet_art(sheet, false, diags, &cooked)) {
            path_join_relative("", sheet->image, path, sizeof(path));
            *ok &= add_unique(files, path);
        } else {
            diag_print_text(diags, stderr);
            *ok = false;
        }
        scene::free_sprite_sheet(sheet);
    }
    delete diags;
}

// A document's own files: the prefabs of its entities or nodes and the images of its Image widgets,
// all relative to the document.
void add_document_files(PathList& files, const scene::Scene* doc, bool* ok) {
    char path[kPath];
    for (uint32_t i = 0; i < doc->entity_count; ++i) {
        const scene::Entity& en = doc->entities[i];
        if (en.prefab[0]) {
            path_join_relative(doc->path, en.prefab, path, sizeof(path));
            *ok &= add_unique(files, path);
        }
        const auto* image = (const ui::Image*)scene::get_component(&en, &ui::kImageDesc);
        if (image && image->path[0]) {
            path_join_relative(doc->path, image->path, path, sizeof(path));
            *ok &= add_unique(files, path);
        }
        const auto* sprite = (const sim::Sprite*)scene::get_component(&en, &sim::kSpriteDesc);
        if (sprite && sprite->sheet[0]) add_sheet_files(files, doc->path, sprite->sheet, ok);
        const auto* look = (const sim::CrowdSprite*)scene::get_component(&en, &sim::kCrowdSpriteDesc);
        if (look && look->sheet[0]) add_sheet_files(files, doc->path, look->sheet, ok);
    }
}

struct DirScan {
    PathList* files;
    const scene::Scene* scene;
    bool ok;
};

// Everything in the scene's directory goes into the APK (a script may name images or screens at
// run time), except scene tests and build extensions with their Java and Kotlin sources, which
// become part of the project instead (D-026).
void add_scanned_file(const char* found, void* user) {
    DirScan& scan = *(DirScan*)user;
    char path[kPath];
    path_join_relative("", found, path, sizeof(path));
    if (ends_with(path, ".test.toml") || starts_with(path, "tests/") || strstr(path, "/tests/")) return;
    // SVG art and the cook's cache: the sheets that use them pack their cooked PNGs (add_sheet_files).
    if (ends_with(path, ".svg") || starts_with(path, ".zaro-cache/") || strstr(path, "/.zaro-cache/")) return;
    if (ends_with(path, ".java") || ends_with(path, ".kt")) return;
    for (uint32_t i = 0; i < scan.scene->build_extension_count; ++i) {
        char extension[kPath];
        path_join_relative("", scan.scene->build_extensions[i], extension, sizeof(extension));
        if (strcmp(path, extension) == 0) return;
    }
    scan.ok &= add_unique(*scan.files, path);
}

void strip_trailing_slash(char* path) {
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') path[--n] = 0;
}

}  // namespace

int export_android(const ExportOptions& o) {
    register_all_components();
    DiagList* diags = new DiagList();
    scene::Scene* s = scene::load_scene(o.scene_path, diags);
    if (!s) {
        diag_print_text(diags, stderr);
        delete diags;
        return 1;
    }
    delete diags;

    // The files the scene needs: itself (first), everything in its directory, and what it names
    // outside it: script, prefabs, screens with their prefabs and images, string tables.
    PathList files;
    files.items = new char[kMaxFiles][kPath];
    bool listed = true;
    char path[kPath];
    path_join_relative("", s->path, path, sizeof(path));
    add_unique(files, path);
    char scene_dir[kPath];
    snprintf(scene_dir, sizeof(scene_dir), "%.*s", (int)dir_length(path), path);
    DirScan scan{&files, s, true};
    platform::find_files(scene_dir[0] ? scene_dir : ".", "", add_scanned_file, &scan);
    listed &= scan.ok;
    if (s->script[0]) {
        path_join_relative("", s->script, path, sizeof(path));
        listed &= add_unique(files, path);
    }
    add_document_files(files, s, &listed);
    // Root screens, then the screens the scene can open by name.
    for (uint32_t i = 0; i < s->ui_count + s->screen_count; ++i) {
        const char* screen_path = i < s->ui_count ? s->ui[i] : s->screens[i - s->ui_count];
        path_join_relative("", screen_path, path, sizeof(path));
        listed &= add_unique(files, path);
        DiagList* screen_diags = new DiagList();
        if (scene::Scene* screen = scene::load_screen(screen_path, screen_diags)) {
            add_document_files(files, screen, &listed);
            scene::free_scene(screen);
        }
        delete screen_diags;
    }
    for (uint32_t i = 0; s->strings[0] && i < s->language_count; ++i) {
        char table[kPath + scene::kLanguageCapacity + 8];
        snprintf(table, sizeof(table), "%s%s.toml", s->strings, s->languages[i]);
        path_join_relative("", table, path, sizeof(path));
        listed &= add_unique(files, path);
    }

    // They are packed below assets/game/ relative to their deepest common directory.
    size_t root = dir_length(files.items[0]);
    for (uint32_t i = 1; i < files.count; ++i) {
        while (root && strncmp(files.items[i], files.items[0], root) != 0) {
            --root;
            while (root && files.items[0][root - 1] != '/') --root;
        }
    }
    int code = 0;
    if (!listed) {
        log_error(kErrExport, "%s: more than %u files to pack", s->path, kMaxFiles);
        code = 1;
    }
    for (uint32_t i = 0; i < files.count && code == 0; ++i) {
        const char* rel = files.items[i] + root;
        if (starts_with(rel, "../") || rel[0] == '/' || (rel[0] && rel[1] == ':')) {
            log_error(kErrExport, "%s: a file of the scene is outside the directory tree it can be packed from; "
                      "run the export from a directory that contains every file of the scene", files.items[i]);
            code = 1;
        }
    }

    char runtime[kPath], out[kPath];
    // Default runtime: build/android/android-runtime next to this binary's build/<preset>/.
    if (o.runtime_dir) path_join_relative("", o.runtime_dir, runtime, sizeof(runtime));
    else path_join_relative(platform::base_path(), "../android/android-runtime", runtime, sizeof(runtime));
    strip_trailing_slash(runtime);
    path_join_relative("", o.out_dir ? o.out_dir : "build/android-project", out, sizeof(out));
    strip_trailing_slash(out);
    char file[kLongPath];
    snprintf(file, sizeof(file), "%s/app/src/main/jniLibs/arm64-v8a/libmain.so", runtime);
    if (code == 0 && platform::file_mtime(file) == 0) {
        log_error(kErrExport, "%s: no Android runtime here; build it with 'python tools/build.py android' "
                  "(or pass --runtime <build>/android-runtime)", runtime);
        code = 1;
    }

    char segment[64], package[128], name[scene::kNameCapacity];
    package_segment(s->name, segment, sizeof(segment));
    if (o.package) snprintf(package, sizeof(package), "%s", o.package);
    else snprintf(package, sizeof(package), "dev.zaro.%s", segment);
    if (code == 0 && !valid_package(package)) {
        log_error(kErrCliUsage, "--package '%s' is not a Java package name like com.example.game", package);
        code = 2;
    }
    display_name(s->name, name, sizeof(name));
    // The game's version (D-025). Google Play allows version codes up to 2100000000 and needs a
    // higher one for every upload; the name is shown to players.
    const char* version_code = o.version_code ? o.version_code : "1";
    const char* version_name = o.version_name ? o.version_name : "1.0";
    char* code_end = nullptr;
    const unsigned long long code_value = strtoull(version_code, &code_end, 10);
    if (code == 0 && (!version_code[0] || *code_end || version_code[0] == '0' || code_value > 2100000000ull)) {
        log_error(kErrCliUsage, "--version-code '%s' must be a whole number from 1 to 2100000000", version_code);
        code = 2;
    }
    if (code == 0 && !valid_version_name(version_name)) {
        log_error(kErrCliUsage, "--version-name '%s' must be 1 to 64 letters, digits, spaces or ._+-", version_name);
        code = 2;
    }
    const char* orientation = o.portrait ? "sensorPortrait" : "sensorLandscape";
    ExtensionText extensions;
    if (!gather_extensions(s, &extensions) && code == 0) code = 1;
    const Substitution subs[] = {
        {"@ZARO_NAME@", name}, {"@ZARO_PACKAGE@", package}, {"@ZARO_VERSION_CODE@", version_code},
        {"@ZARO_VERSION_NAME@", version_name}, {"@ZARO_ORIENTATION@", orientation},
        {"@ZARO_GRADLE_DEPENDENCIES@", extensions.dependencies}, {"@ZARO_MANIFEST_EXTRA@", extensions.manifest},
        {"@ZARO_APPLICATION_EXTRA@", extensions.application},
    };

    Export e;
    e.runtime = runtime;
    e.out = out;
    e.subs = subs;
    e.sub_count = sizeof(subs) / sizeof(subs[0]);
    e.assets.items = new char[kMaxFiles][kPath];
    e.java.items = new char[kMaxFiles][kPath];
    if (code == 0) {
        if (!platform::create_directories(out) || !platform::find_files(runtime, "", copy_runtime_file, &e)) {
            log_error(kErrIo, "%s: cannot create the project", out);
            e.ok = false;
        }
        if (e.ok) copy_extension_sources(e, extensions);
        for (uint32_t i = 0; i < files.count && e.ok; ++i) {
            char rel[kPath + 8], to[kLongPath];
            snprintf(rel, sizeof(rel), "game/%s", files.items[i] + root);
            snprintf(to, sizeof(to), "%s/app/src/main/assets/%s", out, rel);
            if (!make_parent(to) || !platform::copy_file(files.items[i], to)) {
                log_error(kErrIo, "%s: cannot copy to %s", files.items[i], to);
                e.ok = false;
            }
            add_unique(e.assets, rel);
        }
        // The runtime unpacks the files listed here and runs the command line in zaro_args.txt.
        const size_t list_size = (size_t)kMaxFiles * kPath;
        char* list = (char*)calloc(list_size, 1);
        size_t used = 0;
        for (uint32_t i = 0; i < e.assets.count && used < list_size; ++i) {
            used += (size_t)snprintf(list + used, list_size - used, "%s\n", e.assets.items[i]);
        }
        char args[kPath + 16];
        const int args_len = snprintf(args, sizeof(args), "run game/%s\n", files.items[0] + root);
        snprintf(file, sizeof(file), "%s/app/src/main/assets/zaro_assets.txt", out);
        if (e.ok && (used >= list_size || !write_file(file, list, used))) {
            log_error(kErrIo, "%s: cannot write", file);
            e.ok = false;
        }
        free(list);
        snprintf(file, sizeof(file), "%s/app/src/main/assets/zaro_args.txt", out);
        if (e.ok && !write_file(file, args, (size_t)args_len)) {
            log_error(kErrIo, "%s: cannot write", file);
            e.ok = false;
        }
        if (!e.ok) code = 1;
    }
    if (code == 0) {
        printf("exported %s to %s (package %s, %u game files, %u build extensions)\n", s->path, out, package,
               files.count, extensions.count);
        printf("build the APK: gradle -p %s assembleDebug   (python tools/android.py apk does every step)\n", out);
    }
    free_extensions(&extensions);
    delete[] e.java.items;
    delete[] e.assets.items;
    delete[] files.items;
    scene::free_scene(s);
    return code;
}

}  // namespace zaro::app

// Structured diagnostics: every error line is `zaro: error[ZEnnnn]: <message>` on stderr so
// agents can grep by id. Messages that concern a file start with `path:line:` when known.
#pragma once

namespace zaro {

// Stable error ids. Never renumber; add new ids at the end of their group.
enum ErrorId {
    kErrCliUsage = 1,         // ZE0001 bad command line
    kErrPlatformInit = 100,   // ZE0100 SDL / window creation failed
    kErrGpuAdapter = 200,     // ZE0200 no suitable GPU adapter or device
    kErrGpuValidation = 201,  // ZE0201 WebGPU validation or uncaptured error
    kErrShaderLoad = 202,     // ZE0202 shader file missing or unreadable
    kErrGpuReadback = 203,    // ZE0203 buffer map / readback failed
    kErrIo = 300,             // ZE0300 file write failed
    kErrSceneSyntax = 400,    // ZE0400 file missing or not valid TOML
    kErrSceneUnknownComponent = 401,  // ZE0401 table names no registered component
    kErrSceneUnknownField = 402,      // ZE0402 key names no field of the component
    kErrSceneType = 403,      // ZE0403 value has the wrong TOML type for the field
    kErrSceneRange = 404,     // ZE0404 value outside the field's allowed range
    kErrScenePrefab = 405,    // ZE0405 prefab file missing or itself invalid
    kErrSceneStructure = 406, // ZE0406 required key missing or unknown top-level key
    kErrSceneDuplicate = 407, // ZE0407 two entities share a name
    kErrSceneParent = 408,    // ZE0408 a screen node's parent is unknown or comes after it
    kErrSceneToken = 409,     // ZE0409 a "$name" value names no token of the theme, or there is no theme
    kErrBuildExtension = 410, // ZE0410 a build extension's dependency, permission or XML fragment is malformed
    kErrScriptCompile = 500,  // ZE0500 script missing or has a syntax error
    kErrScriptRuntime = 501,  // ZE0501 script raised an error (includes misuse of the zaro API)
    kErrScriptTimeout = 502,  // ZE0502 one script call ran longer than its time budget
    kErrTestSpec = 600,       // ZE0600 test file missing or invalid
    kErrTestExpect = 601,     // ZE0601 an [[expect]] did not hold after the run
    kErrTestStateHash = 602,  // ZE0602 the run's state hash differs from the golden one
    kErrTestClick = 603,      // ZE0603 a [[click]] found no shown, enabled button to click
    kErrFontLoad = 700,       // ZE0700 font file missing or not a TrueType/OpenType font
    kErrImageLoad = 701,      // ZE0701 image missing, not a PNG, or no room in the image atlas
    kErrStringKey = 702,      // ZE0702 an @key text names no entry of a string table
    kErrStringTable = 703,    // ZE0703 string table missing, invalid, or missing a key other languages have
    kErrDesignKit = 704,      // ZE0704 design kit missing or invalid, or the theme it makes is out of date
    kErrExport = 800,         // ZE0800 export failed: runtime package missing or a scene file outside the tree
    kErrSpriteSheet = 900,    // ZE0900 sprite sheet missing or invalid (keys, frames, animations)
    kErrSpriteImage = 901,    // ZE0901 sheet image missing or not a PNG, or a frame outside it
    kErrSpriteRef = 902,      // ZE0902 a sprite component names an animation its sheet does not have
    kErrSvg = 903,            // ZE0903 SVG art missing, invalid, or using what the cook cannot draw (text, filters)
    kErrBuildSpec = 1000,     // ZE1000 build file or [[step]] invalid, or a step names no built-in step
    kErrBuildStep = 1001,     // ZE1001 a build step failed (its program's exit code, or a built-in step's error)
    kErrBuildRelease = 1002,  // ZE1002 a release environment enables debug_tools or a debug* config key
    kErrBuildTarget = 1003,   // ZE1003 a target stage failed, or a release build lacks a release environment,
                              //        a version code or the upload key
    kErrSpawnPrefab = 1200,   // ZE1200 a [scene] prefabs entry has no [prefab] name, or two share one (D-031)
};

void log_error(ErrorId id, const char* fmt, ...);
void log_info(const char* fmt, ...);

}  // namespace zaro

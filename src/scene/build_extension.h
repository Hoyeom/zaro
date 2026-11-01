// Build extensions (D-026): what a plugin adds to an exported Android project, as a text file.
//
//   [build_extension]
//   name = "play-billing"                     # required
//
//   [android]
//   dependencies = ["com.android.billingclient:billing:8.0.0"]  # Gradle group:artifact:version
//   permissions = ["com.android.vending.BILLING"]              # <uses-permission android:name=...>
//   manifest = """<queries><package android:name="com.android.vending" /></queries>"""
//                                             # XML placed inside <manifest>
//   application = """<meta-data android:name="example.key" android:value="1" />"""
//                                             # XML placed inside <application>
//   sources = "android"                       # directory of .java and .kt files, relative to this
//                                             # file; its tree is copied under app/src/main/java
//
// A build extension may also add steps to `zaro build`, in the same [[step]] form as a game build
// file (scene/build_spec.h, D-027):
//
//   [[step]]
//   stage = "pre"
//   name = "billing-client"
//   run = ["python", "gen/billing.py"]
//
// Every key of [android] is optional. A scene names its extensions in [scene]:
//
//   build_extensions = ["../plugins/billing/build_extension.toml"]
//
// `zaro check` loads them with the scene and reports problems with file and line (ZE0410 for a
// malformed dependency, permission or XML fragment). `zaro export android` adds the dependencies to
// app/build.gradle, the permissions and fragments to AndroidManifest.xml, and copies the sources.
#pragma once

#include "core/diag.h"
#include "scene/build_spec.h"

#include <stdint.h>

namespace zaro::scene {

constexpr uint32_t kMaxExtensionItems = 32;
constexpr uint32_t kExtensionItemCapacity = 160;

struct BuildExtension {
    char path[260];
    char name[64];
    char dependencies[kMaxExtensionItems][kExtensionItemCapacity];
    uint32_t dependency_count;
    char permissions[kMaxExtensionItems][kExtensionItemCapacity];
    uint32_t permission_count;
    char* manifest_xml;     // inside <manifest>, or nullptr
    char* application_xml;  // inside <application>, or nullptr
    char sources[260];      // resolved directory, or empty
    int sources_line;
    BuildStep* steps;       // kMaxBuildSteps slots; step_count used
    uint32_t step_count;
};

// Loads and validates a build extension file. Problems are diagnostics with file and line; returns
// nullptr if there was any.
BuildExtension* load_build_extension(const char* path, DiagList* diags);
void free_build_extension(BuildExtension* extension);

}  // namespace zaro::scene

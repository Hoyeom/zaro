#include "scene/sound_bank.h"

#include "core/path.h"
#include "core/reflect.h"

#include <tomlc17.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

const char* const kSynthWaveNames[] = {"sine", "square", "triangle", "saw", "noise", nullptr};

namespace {

bool is_number(const toml_datum_t& v) { return v.type == TOML_INT64 || v.type == TOML_FP64; }
double number(const toml_datum_t& v) { return v.type == TOML_INT64 ? (double)v.u.int64 : v.u.fp64; }

// A number in [lo, hi]; false with a diagnostic otherwise.
bool read_number(const toml_datum_t& v, double lo, double hi, double* out, const char* what, const char* file,
                 DiagList* d) {
    if (!is_number(v) || number(v) < lo || number(v) > hi) {
        diag_add(d, kErrSoundFile, file, v.lineno, v.colno, "%s must be a number in %g..%g", what, lo, hi);
        return false;
    }
    *out = number(v);
    return true;
}

bool read_bool(const toml_datum_t& v, bool* out, const char* what, const char* file, DiagList* d) {
    if (v.type != TOML_BOOLEAN) {
        diag_add(d, kErrSoundFile, file, v.lineno, v.colno, "%s must be true or false", what);
        return false;
    }
    *out = v.u.boolean;
    return true;
}

bool read_name(const toml_datum_t& v, char* out, size_t size, const char* what, const char* file, DiagList* d) {
    if (v.type != TOML_STRING || !v.u.str.ptr[0] || strlen(v.u.str.ptr) >= size) {
        diag_add(d, kErrSoundFile, file, v.lineno, v.colno, "%s must be a non-empty string under %u bytes", what,
                 (unsigned)size);
        return false;
    }
    memcpy(out, v.u.str.ptr, strlen(v.u.str.ptr) + 1);
    return true;
}

bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

// read_number into a float field.
bool read_float(const toml_datum_t& v, double lo, double hi, float* out, const char* what, const char* file,
                DiagList* d) {
    double x = 0.0;
    if (!read_number(v, lo, hi, &x, what, file, d)) return false;
    *out = (float)x;
    return true;
}

bool read_wave(const toml_datum_t& v, uint8_t* out, const char* file, DiagList* d) {
    for (uint8_t i = 0; v.type == TOML_STRING && kSynthWaveNames[i]; ++i) {
        if (strcmp(v.u.str.ptr, kSynthWaveNames[i]) == 0) {
            *out = i;
            return true;
        }
    }
    diag_add(d, kErrSoundFile, file, v.lineno, v.colno, "wave must be sine, square, triangle, saw or noise");
    return false;
}

// "A4" is 440 Hz; "C#3", "Bb2"; octaves 0..8. 0 if it is not a note.
float note_hz(const char* s, size_t n) {
    static const int kSemitone[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    if (n < 2 || n > 3 || s[0] < 'A' || s[0] > 'G') return 0.0f;
    int semitone = kSemitone[s[0] - 'A'];
    size_t i = 1;
    if (n == 3) {
        if (s[1] == '#') ++semitone;
        else if (s[1] == 'b') --semitone;
        else return 0.0f;
        i = 2;
    }
    if (s[i] < '0' || s[i] > '8') return 0.0f;
    const int midi = 12 * (s[i] - '0' + 1) + semitone;
    return (float)(440.0 * pow(2.0, (midi - 69) / 12.0));
}

bool read_tone(SoundBank* b, Sound* s, const toml_datum_t& t, DiagList* d) {
    if (t.type != TOML_TABLE) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno,
                 "synth must be a table: { wave = .., from = .., time = .. }");
        return false;
    }
    SynthTone& tone = s->tone;
    tone = SynthTone{kWaveSine, 440.0f, -1.0f, 0.0f, 0.005f, 0.05f};
    bool ok = true;
    for (int32_t k = 0; k < t.u.tab.size; ++k) {
        const char* key = t.u.tab.key[k];
        const toml_datum_t& v = t.u.tab.value[k];
        if (strcmp(key, "wave") == 0) ok = read_wave(v, &tone.wave, b->path, d) && ok;
        else if (strcmp(key, "from") == 0) ok = read_float(v, 1.0, 24000.0, &tone.from, "from", b->path, d) && ok;
        else if (strcmp(key, "to") == 0) ok = read_float(v, 1.0, 24000.0, &tone.to, "to", b->path, d) && ok;
        else if (strcmp(key, "time") == 0) ok = read_float(v, 0.005, 10.0, &tone.time, "time", b->path, d) && ok;
        else if (strcmp(key, "attack") == 0) ok = read_float(v, 0.0, 10.0, &tone.attack, "attack", b->path, d) && ok;
        else if (strcmp(key, "release") == 0) ok = read_float(v, 0.0, 10.0, &tone.release, "release", b->path, d) && ok;
        else {
            diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno,
                     "unexpected key '%s' in synth (expected wave, from, to, time, attack, release)", key);
            ok = false;
        }
    }
    if (tone.to < 0.0f) tone.to = tone.from;
    if (ok && tone.time <= 0.0f) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "synth needs time = <seconds>");
        ok = false;
    }
    return ok;
}

// The notes of a track string into b->notes. False (with a diagnostic) on a token that is not a note.
bool read_notes(SoundBank* b, SynthTrack* tr, const toml_datum_t& v, DiagList* d) {
    for (const char* p = v.u.str.ptr; *p;) {
        while (*p == ' ' || *p == '\t' || *p == '|') ++p;  // '|' may mark bars
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '|') ++p;
        const size_t n = (size_t)(p - start);
        ++tr->steps;
        // "-" holds the note before; at the start of a track it is a rest.
        if (n == 1 && *start == '-' && tr->note_count) {
            ++b->notes[b->note_count - 1].steps;
            continue;
        }
        float hz = 0.0f;
        if (!(n == 1 && (*start == '.' || *start == '-'))) {
            hz = note_hz(start, n);
            if (hz <= 0.0f) {
                diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno,
                         "'%.*s' is not a note (A4, C#3, Bb2), '.' or '-'", (int)n, start);
                return false;
            }
        }
        if (b->note_count == kMaxSynthNotes) {
            diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno, "more than %u pattern notes in the file",
                     kMaxSynthNotes);
            return false;
        }
        b->notes[b->note_count++] = SynthNote{hz, 1};
        ++tr->note_count;
    }
    return true;
}

bool read_track(SoundBank* b, const toml_datum_t& t, DiagList* d) {
    if (t.type != TOML_TABLE) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "each track must be a table: { wave = .., notes = .. }");
        return false;
    }
    if (b->track_count == kMaxSynthTracks) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "more than %u pattern tracks in the file",
                 kMaxSynthTracks);
        return false;
    }
    SynthTrack tr = {kWaveSine, 1.0f, 0.005f, 0.05f, b->note_count, 0, 0};
    bool ok = true, has_notes = false;
    for (int32_t k = 0; k < t.u.tab.size; ++k) {
        const char* key = t.u.tab.key[k];
        const toml_datum_t& v = t.u.tab.value[k];
        if (strcmp(key, "wave") == 0) {
            ok = read_wave(v, &tr.wave, b->path, d) && ok;
        } else if (strcmp(key, "volume") == 0) {
            ok = read_float(v, 0.0, 1.0, &tr.volume, "volume", b->path, d) && ok;
        } else if (strcmp(key, "attack") == 0) {
            ok = read_float(v, 0.0, 10.0, &tr.attack, "attack", b->path, d) && ok;
        } else if (strcmp(key, "release") == 0) {
            ok = read_float(v, 0.0, 10.0, &tr.release, "release", b->path, d) && ok;
        } else if (strcmp(key, "notes") == 0 && v.type == TOML_STRING) {
            has_notes = true;
            ok = read_notes(b, &tr, v, d) && ok;
        } else if (strcmp(key, "notes") == 0) {
            diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno, "notes must be a string such as \"A2 . C3 -\"");
            ok = false;
        } else {
            diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno,
                     "unexpected key '%s' in a track (expected wave, notes, volume, attack, release)", key);
            ok = false;
        }
    }
    if (ok && (!has_notes || !tr.steps)) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "a track needs notes = \"...\" with at least one step");
        ok = false;
    }
    if (!ok) {
        b->note_count = tr.first_note;
        return false;
    }
    b->tracks[b->track_count++] = tr;
    return true;
}

bool read_pattern(SoundBank* b, Sound* s, const toml_datum_t& t, DiagList* d) {
    if (t.type != TOML_TABLE) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "pattern must be a [sound.pattern] table");
        return false;
    }
    s->first_track = b->track_count;
    bool ok = true;
    for (int32_t k = 0; k < t.u.tab.size; ++k) {
        const char* key = t.u.tab.key[k];
        const toml_datum_t& v = t.u.tab.value[k];
        if (strcmp(key, "step") == 0) {
            ok = read_float(v, 0.01, 10.0, &s->step, "step", b->path, d) && ok;
        } else if (strcmp(key, "tracks") == 0 && v.type == TOML_ARRAY) {
            for (int32_t i = 0; i < v.u.arr.size; ++i) ok = read_track(b, v.u.arr.elem[i], d) && ok;
        } else {
            diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno,
                     "unexpected key '%s' in pattern (expected step and a tracks array)", key);
            ok = false;
        }
    }
    s->track_count = b->track_count - s->first_track;
    for (uint32_t i = 0; i < s->track_count; ++i) {
        const uint32_t steps = b->tracks[s->first_track + i].steps;
        if (steps > s->steps) s->steps = steps;
    }
    if (ok && (s->step <= 0.0f || !s->track_count)) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "pattern needs step = <seconds> and at least one track");
        ok = false;
    }
    if (ok && (double)s->step * s->steps > 60.0) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "pattern lasts %.1f s; at most 60 s",
                 (double)s->step * s->steps);
        ok = false;
    }
    return ok;
}

void load_groups(SoundBank* b, const toml_datum_t& groups, DiagList* d) {
    for (int32_t i = 0; i < groups.u.arr.size; ++i) {
        const toml_datum_t& t = groups.u.arr.elem[i];
        if (t.type != TOML_TABLE) continue;
        if (b->group_count == kMaxSoundGroups) {
            diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "more than %u groups", kMaxSoundGroups - 1);
            return;
        }
        SoundGroup g = {};
        g.volume = 1.0f;
        g.line = t.lineno;
        bool named = false;
        for (int32_t k = 0; k < t.u.tab.size; ++k) {
            const char* key = t.u.tab.key[k];
            const toml_datum_t& v = t.u.tab.value[k];
            double x;
            if (strcmp(key, "name") == 0) {
                named = read_name(v, g.name, sizeof(g.name), "group name", b->path, d);
            } else if (strcmp(key, "volume") == 0) {
                if (read_number(v, 0.0, 1.0, &x, "volume", b->path, d)) g.volume = (float)x;
            } else if (strcmp(key, "save") == 0) {
                if (read_name(v, g.save, sizeof(g.save), "save", b->path, d)) g.save_line = v.lineno;
            } else {
                diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno,
                         "unexpected key '%s' in [[group]] (expected name, volume, save)", key);
            }
        }
        if (!named) {
            if (t.u.tab.size == 0 || toml_get(t, "name").type == TOML_UNKNOWN) {
                diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "[[group]] needs a name");
            }
            continue;
        }
        if (find_sound_group(b, g.name) >= 0) {
            diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "group '%s' is already defined%s", g.name,
                     strcmp(g.name, "master") == 0 ? " (master always exists)" : "");
            continue;
        }
        b->groups[b->group_count++] = g;
    }
}

void load_sound(SoundBank* b, const toml_datum_t& t, DiagList* d) {
    Sound s = {};
    s.volume = 1.0f;
    s.pitch_min = s.pitch_max = 1.0f;
    s.voices = 8;
    s.line = t.lineno;
    bool named = false, synth_ok = true;
    int file_line = 0, synth_line = 0, pattern_line = 0;
    char file[260] = {};
    for (int32_t k = 0; k < t.u.tab.size; ++k) {
        const char* key = t.u.tab.key[k];
        const toml_datum_t& v = t.u.tab.value[k];
        double x;
        if (strcmp(key, "name") == 0) {
            named = read_name(v, s.name, sizeof(s.name), "sound name", b->path, d);
        } else if (strcmp(key, "file") == 0) {
            if (read_name(v, file, sizeof(file), "file", b->path, d)) file_line = v.lineno;
        } else if (strcmp(key, "group") == 0) {
            char group[64];
            if (!read_name(v, group, sizeof(group), "group", b->path, d)) continue;
            const int g = find_sound_group(b, group);
            if (g < 0) {
                const char* hint = suggest_sound_group(b, group);
                diag_add(d, kErrSoundRef, b->path, v.lineno, v.colno, "no group '%s'%s%s%s", group,
                         hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
                continue;
            }
            s.group = (uint32_t)g;
        } else if (strcmp(key, "volume") == 0) {
            if (read_number(v, 0.0, 1.0, &x, "volume", b->path, d)) s.volume = (float)x;
        } else if (strcmp(key, "pitch") == 0) {
            const bool pair = v.type == TOML_ARRAY && v.u.arr.size == 2 && is_number(v.u.arr.elem[0]) &&
                              is_number(v.u.arr.elem[1]);
            const double lo = pair ? number(v.u.arr.elem[0]) : 0.0, hi = pair ? number(v.u.arr.elem[1]) : 0.0;
            if (!pair || lo < 0.25 || hi > 4.0 || lo > hi) {
                diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno,
                         "pitch must be [low, high] with 0.25 <= low <= high <= 4");
                continue;
            }
            s.pitch_min = (float)lo;
            s.pitch_max = (float)hi;
        } else if (strcmp(key, "voices") == 0) {
            if (v.type != TOML_INT64 || v.u.int64 < 1 || v.u.int64 > 64) {
                diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno, "voices must be an integer in 1..64");
                continue;
            }
            s.voices = (uint32_t)v.u.int64;
        } else if (strcmp(key, "cooldown") == 0) {
            if (read_number(v, 0.0, 60.0, &x, "cooldown", b->path, d)) s.cooldown = (float)x;
        } else if (strcmp(key, "stream") == 0) {
            read_bool(v, &s.stream, "stream", b->path, d);
        } else if (strcmp(key, "loop") == 0) {
            read_bool(v, &s.loop, "loop", b->path, d);
        } else if (strcmp(key, "synth") == 0) {
            synth_line = v.lineno;
            synth_ok = read_tone(b, &s, v, d) && synth_ok;
        } else if (strcmp(key, "pattern") == 0) {
            pattern_line = v.lineno;
            synth_ok = read_pattern(b, &s, v, d) && synth_ok;
        } else {
            diag_add(d, kErrSoundFile, b->path, v.lineno, v.colno,
                     "unexpected key '%s' in [[sound]] (expected name, file or synth or pattern, group, "
                     "volume, pitch, voices, cooldown, stream, loop)", key);
        }
    }
    const int sources = (file_line ? 1 : 0) + (synth_line ? 1 : 0) + (pattern_line ? 1 : 0);
    if (!named || sources != 1) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno,
                 "[[sound]] needs a name and one of file, synth or pattern");
        return;
    }
    if (find_sound(b, s.name) >= 0) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "sound '%s' is already defined", s.name);
        return;
    }
    if (!synth_ok) return;
    if (!file_line) {
        s.source = synth_line ? Sound::kTone : Sound::kPattern;
        if (b->sound_count == kMaxSounds) {
            diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "more than %u sounds", kMaxSounds);
            return;
        }
        b->sounds[b->sound_count++] = s;
        return;
    }
    const char* ext = strrchr(file, '.');
    if (!ext || (strcmp(ext, ".wav") != 0 && strcmp(ext, ".ogg") != 0 && strcmp(ext, ".mp3") != 0 &&
                 strcmp(ext, ".flac") != 0)) {
        diag_add(d, kErrSoundAsset, b->path, file_line, 1, "'%s' is not a .wav, .ogg, .mp3 or .flac file", file);
        return;
    }
    path_join_relative(b->path, file, s.file, sizeof(s.file));
    if (!file_exists(s.file)) {
        diag_add(d, kErrSoundAsset, b->path, file_line, 1, "cannot open '%s'", s.file);
        return;
    }
    if (b->sound_count == kMaxSounds) {
        diag_add(d, kErrSoundFile, b->path, t.lineno, t.colno, "more than %u sounds", kMaxSounds);
        return;
    }
    b->sounds[b->sound_count++] = s;
}

}  // namespace

SoundBank* load_sound_bank(const char* path, DiagList* d) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        diag_add(d, kErrSoundFile, path, 0, 0, "cannot open sound file");
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)size + 1);
    const size_t n = fread(text, 1, (size_t)size, f);
    text[n] = 0;
    fclose(f);

    SoundBank* b = (SoundBank*)calloc(1, sizeof(SoundBank));
    snprintf(b->path, sizeof(b->path), "%s", path);
    memcpy(b->groups[0].name, "master", 7);
    b->groups[0].volume = 1.0f;
    b->group_count = 1;

    const uint32_t before = d->count;
    toml_result_t r = toml_parse_named(text, (int)n, path);
    if (!r.ok) {
        diag_add(d, kErrSceneSyntax, path, 0, 0, "invalid TOML: %s", r.errmsg);
    } else {
        const toml_datum_t top = r.toptab;
        for (int32_t i = 0; i < top.u.tab.size; ++i) {
            const char* key = top.u.tab.key[i];
            const toml_datum_t& v = top.u.tab.value[i];
            const bool known = strcmp(key, "group") == 0 || strcmp(key, "sound") == 0;
            if (!known || v.type != TOML_ARRAY) {
                diag_add(d, kErrSoundFile, path, v.lineno, v.colno,
                         "unexpected top-level key '%s' (expected [[group]] and [[sound]] tables)", key);
            }
        }
        // Groups first, so a sound may name a group defined below it.
        const toml_datum_t groups = toml_get(top, "group");
        if (groups.type == TOML_ARRAY) load_groups(b, groups, d);
        const toml_datum_t sounds = toml_get(top, "sound");
        for (int32_t i = 0; sounds.type == TOML_ARRAY && i < sounds.u.arr.size; ++i) {
            if (sounds.u.arr.elem[i].type == TOML_TABLE) load_sound(b, sounds.u.arr.elem[i], d);
        }
    }
    if (r.ok || r.__internal) toml_free(r);
    free(text);
    if (d->count != before) {
        free(b);
        return nullptr;
    }
    return b;
}

void free_sound_bank(SoundBank* b) { free(b); }

int find_sound(const SoundBank* b, const char* name) {
    for (uint32_t i = 0; i < b->sound_count; ++i) {
        if (strcmp(b->sounds[i].name, name) == 0) return (int)i;
    }
    return -1;
}

int find_sound_group(const SoundBank* b, const char* name) {
    for (uint32_t i = 0; i < b->group_count; ++i) {
        if (strcmp(b->groups[i].name, name) == 0) return (int)i;
    }
    return -1;
}

const char* suggest_sound(const SoundBank* b, const char* name) {
    const char* names[kMaxSounds];
    for (uint32_t i = 0; i < b->sound_count; ++i) names[i] = b->sounds[i].name;
    return reflect::suggest_name(name, names, b->sound_count);
}

const char* suggest_sound_group(const SoundBank* b, const char* name) {
    const char* names[kMaxSoundGroups];
    for (uint32_t i = 0; i < b->group_count; ++i) names[i] = b->groups[i].name;
    return reflect::suggest_name(name, names, b->group_count);
}

}  // namespace zaro::scene

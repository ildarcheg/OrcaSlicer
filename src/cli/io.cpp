#include "io.hpp"
#include "invariants.hpp"
#include "output.hpp"
#include "png_placeholder.hpp"

#include <libslic3r/Format/bbs_3mf.hpp>
#include <libslic3r/Semver.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/miniz_extension.hpp>

#include <boost/filesystem.hpp>
#include <regex>
#include <stdexcept>
#include <unordered_set>

namespace orca_cli {

namespace fs = boost::filesystem;

namespace {

using namespace Slic3r; // brings open_zip_reader / close_zip_reader / open_zip_writer / close_zip_writer into scope

// Pattern for files that store_bbs_3mf references via .rels but does not
// emit unless thumbnail_data is provided in StoreParams. Plate-numbered
// PNG blobs only. The first capture group is the trailing integer N
// (1-based plate number on disk); we use it to skip orphaned entries
// whose N exceeds the current plate count (spec section 4.2: plate
// remove must "drop their PNGs").
static const std::regex thumbnail_re(
    R"(^Metadata/(?:plate_(\d+)(?:_small)?|plate_no_light_(\d+)|top_(\d+)|pick_(\d+))\.png$)");

// Returns true if `name` is a plate-numbered thumbnail whose trailing
// integer N exceeds plate_count -- i.e. an orphan left over from a
// `plate remove`. Spec section 4.2 requires we drop it.
bool is_orphan_plate_png(const std::string& name, int plate_count)
{
    std::smatch m;
    if (!std::regex_match(name, m, thumbnail_re)) return false;
    for (size_t g = 1; g < m.size(); ++g) {
        if (m[g].matched) {
            try {
                return std::stoi(m[g].str()) > plate_count;
            } catch (...) { return false; }
        }
    }
    return false;
}

// Phase 1: Open target zip, enumerate entries, identify orphan plate PNGs.
// Closes the archive before returning so no zip handle escapes this function.
//
// kept_names  — all non-directory entries that are not orphans; retained for
//               to_synthesize planning.
// orphan_names — plate-numbered PNGs whose trailing N exceeds plate_count;
//                these are dropped so `plate remove` cleans up its PNGs.
// opened      — false if the archive could not be opened (sentinel).
//
// Note: blobs are no longer extracted here. rewrite_archive_with_blobs opens
// its own reader and uses mz_zip_writer_add_from_zip_reader for zero-copy
// passthrough of carried-forward entries (T14).
struct TargetEntryInfo {
    std::unordered_set<std::string> kept_names;
    std::unordered_set<std::string> orphan_names;
    bool                            opened = false;
};

TargetEntryInfo enumerate_target_entries(const std::string& target_zip_path,
                                         int plate_count)
{
    TargetEntryInfo info;
    mz_zip_archive tgt_reader{};
    if (!open_zip_reader(&tgt_reader, target_zip_path))
        return info; // info.opened == false signals the caller to skip

    info.opened = true;
    const mz_uint tgt_count = mz_zip_reader_get_num_files(&tgt_reader);
    info.kept_names.reserve(tgt_count);
    for (mz_uint i = 0; i < tgt_count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&tgt_reader, i, &st)) continue;
        if (st.m_is_directory)                             continue;
        std::string name(st.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        // Note: we intentionally record orphan plate PNG names found in the
        // target into a separate set so we can both
        //   (a) avoid carrying them forward in the rewrite phase, and
        //   (b) keep them out of `kept_names` for to_synthesize / to_copy
        //       planning, since those orphan slots will be removed.
        if (is_orphan_plate_png(name, plate_count)) {
            info.orphan_names.insert(name);
            continue;
        }
        info.kept_names.insert(name);
    }
    close_zip_reader(&tgt_reader);
    return info;
}

// Phase 2 + 3: Open source zip (if available), enumerate, extract source
// blobs that are missing in target, and determine which placeholder PNGs to
// synthesize. Closes the source archive before returning — no zip handle
// escapes this function.
//
// source_blobs — extracted (name, bytes) for thumbnail entries that are
//                present in source but absent in target.
// to_synthesize — names for which a placeholder PNG must be injected.
struct PassthroughPlan {
    std::vector<std::pair<std::string, std::vector<unsigned char>>> source_blobs;
    std::vector<std::string>                                        to_synthesize;
};

PassthroughPlan plan_thumbnail_passthrough(const ProjectState&    s,
                                           const TargetEntryInfo& target)
{
    namespace fs = boost::filesystem;
    PassthroughPlan plan;
    const int plate_count = static_cast<int>(s.plates.size());

    // Extract source blobs: thumbnail entries present in source but missing
    // in target. Best-effort -- if the source archive cannot be opened, skip
    // the copy phase and proceed to placeholder injection (which is what
    // `plate add` on an in-memory ProjectState needs anyway).
    const std::string& source_zip_path = s.source_path;
    const bool has_source =
        !source_zip_path.empty() && fs::exists(source_zip_path);

    if (has_source) {
        mz_zip_archive src_reader{};
        if (open_zip_reader(&src_reader, source_zip_path)) {
            const mz_uint src_count = mz_zip_reader_get_num_files(&src_reader);
            for (mz_uint i = 0; i < src_count; ++i) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&src_reader, i, &st)) continue;
                if (st.m_is_directory)                              continue;
                std::string name(st.m_filename);
                std::replace(name.begin(), name.end(), '\\', '/');
                if (!std::regex_match(name, thumbnail_re)) continue;
                // Spec section 4.2: a plate that no longer exists in
                // ProjectState must not have its PNGs carried forward.
                if (is_orphan_plate_png(name, plate_count)) continue;
                if (target.kept_names.count(name))          continue;
                std::vector<unsigned char> bytes(static_cast<size_t>(st.m_uncomp_size));
                if (!mz_zip_reader_extract_to_mem(&src_reader, i, bytes.data(), bytes.size(), 0))
                    continue;
                plan.source_blobs.emplace_back(std::move(name), std::move(bytes));
            }
            close_zip_reader(&src_reader);
        }
    }

    // Plan placeholder injections: for every plate index 1..plates.size(),
    // if neither the target nor the source-blob plan covers
    // Metadata/plate_<n>.png and Metadata/plate_<n>_small.png, synthesize a
    // 128x128 gray placeholder. Plate numbering is 1-based on disk (see
    // verify_plate_thumbnails / store_bbs_3mf).
    std::unordered_set<std::string> planned_copy_names;
    planned_copy_names.reserve(plan.source_blobs.size());
    for (const auto& [name, bytes] : plan.source_blobs)
        planned_copy_names.insert(name);

    for (size_t i = 1; i <= s.plates.size(); ++i) {
        const auto t = orca_cli::plate_thumbnail_paths(int(i));
        for (const std::string& name : {t.mid, t.small}) {
            if (target.kept_names.count(name))     continue;
            if (planned_copy_names.count(name))    continue;
            plan.to_synthesize.push_back(name);
        }
    }

    return plan;
}

// Phase 4: Write rewrite archive using raw zip-to-zip copy for carried-forward
// entries (T14: zero-recompress passthrough). Opens the target archive as a
// reader here, then writes the rewrite archive (target blobs + plan.source_blobs
// + placeholder injections), and closes both archives before returning.
//
// mz_zip_writer_add_from_zip_reader copies raw deflated bytes from tgt_reader
// to writer without decompressing or recompressing. For the ~99% of entries that
// are unchanged on a typical save (mesh blobs, config blobs, untouched PNGs),
// this eliminates both the extract and re-deflate passes. On the rare miniz
// failure we fall back to a full decompress+recompress for that entry only.
void rewrite_archive_with_blobs(const std::string&       target_zip_path,
                                const std::string&       rewrite_path,
                                const TargetEntryInfo&   target,
                                const PassthroughPlan&   plan,
                                const std::vector<char>& placeholder)
{
    mz_zip_archive tgt_reader{};
    if (!open_zip_reader(&tgt_reader, target_zip_path)) return;

    mz_zip_archive writer{};
    if (!open_zip_writer(&writer, rewrite_path)) {
        close_zip_reader(&tgt_reader);
        return;
    }

    // Carry forward every non-orphan entry as raw deflated bytes.
    const mz_uint tgt_count = mz_zip_reader_get_num_files(&tgt_reader);
    for (mz_uint i = 0; i < tgt_count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&tgt_reader, i, &st)) continue;
        if (st.m_is_directory) continue;
        std::string name(st.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        if (target.orphan_names.count(name)) continue;
        if (!mz_zip_writer_add_from_zip_reader(&writer, &tgt_reader, i)) {
            // Fallback: decompress + recompress on the rare miniz failure.
            std::vector<unsigned char> bytes(static_cast<size_t>(st.m_uncomp_size));
            if (mz_zip_reader_extract_to_mem(&tgt_reader, i, bytes.data(), bytes.size(), 0)) {
                mz_zip_writer_add_mem(&writer, name.c_str(), bytes.data(), bytes.size(),
                                      MZ_DEFAULT_COMPRESSION);
            }
        }
    }

    // Source-copied blobs were already extracted to memory by
    // plan_thumbnail_passthrough; write them as-is. PNG is already
    // deflate-compressed, so MZ_NO_COMPRESSION avoids double-deflate.
    for (const auto& kv : plan.source_blobs) {
        mz_zip_writer_add_mem(&writer, kv.first.c_str(),
                              kv.second.data(), kv.second.size(),
                              MZ_NO_COMPRESSION);
    }

    // Synthesized placeholder PNGs (one buffer reused for every plate).
    for (const std::string& name : plan.to_synthesize) {
        mz_zip_writer_add_mem(&writer, name.c_str(),
                              placeholder.data(), placeholder.size(),
                              MZ_NO_COMPRESSION);
    }

    mz_zip_writer_finalize_archive(&writer);
    close_zip_writer(&writer);
    close_zip_reader(&tgt_reader);
}

// Phase 5 (swap): Atomic rename-swap: rename target -> .bak, rename
// rewrite -> target, remove .bak. If anything fails midway, restore the
// original via the .bak so we never end up with the target file missing on
// disk.
void atomic_swap_rewrite(const std::string& target_zip_path,
                         const std::string& rewrite_path)
{
    namespace fs = boost::filesystem;

    // Atomic rename-swap: rename target -> .bak, rename rewrite -> target,
    // remove .bak. If anything fails midway, restore the original via the .bak
    // so we never end up with the target file missing on disk. The previous
    // remove+rename sequence could lose the .tmp file on a rare
    // rename-failure path and produce a misleading "cannot open archive"
    // error downstream from run_all_invariants.
    const fs::path bak = fs::path(target_zip_path + ".bak");
    boost::system::error_code ec;
    fs::rename(target_zip_path, bak, ec);
    if (ec) {
        // Couldn't even start the swap -- abort the passthrough cleanly.
        boost::system::error_code ec2;
        fs::remove(rewrite_path, ec2);
        throw InvariantViolation(
            "thumbnail passthrough rewrite-swap failed (could not rename target): " + ec.message());
    }
    fs::rename(rewrite_path, target_zip_path, ec);
    if (ec) {
        // Restore the original so we don't lose data.
        boost::system::error_code ec2;
        fs::rename(bak, target_zip_path, ec2);
        fs::remove(rewrite_path, ec2);
        throw InvariantViolation(
            "thumbnail passthrough rewrite-swap failed (could not install rewrite): " + ec.message());
    }
    fs::remove(bak, ec);
}

// store_bbs_3mf always writes relationship entries pointing at
//   /Metadata/plate_<n>.png            (cover-thumbnail-middle)
//   /Metadata/plate_<n>_small.png      (cover-thumbnail-small)
// regardless of whether those files actually get written into the archive.
// When the CLI is just cloning a known-good source 3mf (the `project init`
// case) we have those exact PNG blobs sitting in the source archive already
// -- carry them through verbatim so the resulting archive's relationships
// file does not dangle.
//
// We additionally copy through plate_no_light_<n>.png, top_<n>.png, and
// pick_<n>.png when they exist in the source: store_bbs_3mf does NOT add
// relationship entries for those files, but the Orca GUI loads them when
// present and falls back gracefully when absent, so they are safe to copy
// through and improve fidelity for the clone-and-mutate flow.
//
// When a plate exists in the project that does NOT have a corresponding
// PNG entry in either the target or the source (i.e. a plate created by
// `plate add` -- there is no source PNG to copy through), we inject a
// 128x128 gray placeholder PNG (orca_cli::make_placeholder_png_128_gray_C0)
// for both Metadata/plate_<n>.png and Metadata/plate_<n>_small.png. This
// keeps the runtime invariant guard (verify_plate_thumbnails + the
// dangling-relationships check) happy without forcing the CLI to render.
//
// This function falls through (no rewrite) when:
//   * the target archive cannot be opened (best-effort);
//   * there is nothing to copy from source AND no placeholder injections
//     are required.
//
// The output archive is rewritten in place: we open it as a reader, build a
// list of entries to keep + entries to copy from source + entries to
// synthesize, then write a new archive at the same path. miniz lacks a
// robust in-place append API for arbitrary zip files
// (`mz_zip_writer_init_from_reader` requires the source to be writable
// and has alignment constraints we cannot rely on).
void passthrough_missing_thumbnails(const ProjectState& s,
                                    const std::string&  target_zip_path)
{
    // Phase 1: open, enumerate, extract, close — all inside the helper.
    auto target = enumerate_target_entries(target_zip_path, int(s.plates.size()));
    if (!target.opened) {
        // Archive could not be opened — best-effort fallthrough, nothing to do.
        return;
    }

    // Phase 2+3: open source, extract blobs, close — all inside the helper.
    auto plan = plan_thumbnail_passthrough(s, target);

    if (plan.source_blobs.empty() &&
        plan.to_synthesize.empty()  &&
        target.orphan_names.empty()) {
        return;
    }

    // Build the placeholder PNG bytes once and reuse for every synthesized
    // entry. Both plate_N.png and plate_N_small.png get the same 128x128
    // gray placeholder; the GUI may render the same image at two sizes but
    // will not fail to load or crash. This is acceptable for v1.
    std::vector<char> placeholder;
    if (!plan.to_synthesize.empty())
        placeholder = make_placeholder_png_128_gray_C0();

    // Phase 4: write rewrite archive — tgt_reader opened internally, raw
    // zip-to-zip copy for carried-forward entries, no zip handles escape.
    const std::string rewrite_path = target_zip_path + ".rewrite";
    rewrite_archive_with_blobs(target_zip_path, rewrite_path, target, plan, placeholder);
    atomic_swap_rewrite(target_zip_path, rewrite_path);
}

} // namespace

ProjectState load_project(const std::string& path)
{
    using namespace Slic3r;

    ProjectState s;
    s.model          = std::make_unique<Model>();
    s.project_config = std::make_unique<DynamicPrintConfig>();
    s.source_path    = path;

    PlateDataPtrs plate_data;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    bool   is_bbl_3mf  = false;
    bool   is_orca_3mf = false;
    Semver file_version;

    const bool ok = load_bbs_3mf(
        path.c_str(),
        s.project_config.get(),
        &substitutions,
        s.model.get(),
        &plate_data,
        /*project_presets*/ nullptr,
        &is_bbl_3mf,
        &is_orca_3mf,
        &file_version,
        /*proFn*/ nullptr,
        LoadStrategy::LoadModel | LoadStrategy::LoadConfig);

    if (!ok) {
        // load_bbs_3mf may have populated plate_data partially; release any
        // entries it owns so we don't leak.
        release_PlateData_list(plate_data);
        throw std::runtime_error("load_bbs_3mf failed: " + path);
    }

    // Take ownership of every PlateData* that the loader allocated.
    for (PlateData* pd : plate_data)
        s.plates.emplace_back(pd);
    // Clear plate_data so release_PlateData_list (if ever called again) does
    // not double-free; we now own the pointers via unique_ptr in s.plates.
    plate_data.clear();

    // Rebuild PlateData::objects_and_instances from the obj_inst_map <-> loaded_id
    // pairing. The loader fills in obj_inst_map (plate-side identify_id mapping)
    // but does not always rebuild objects_and_instances in the form the saver
    // expects, so re-derive it here from ModelInstance::loaded_id.
    //
    // PlateData::obj_inst_map is std::map<int, std::pair<int, int>> keyed by
    // object_id, with value pair<instance_id, identify_id>. The identify_id
    // that matches ModelInstance::loaded_id lives in `value.second`, not in
    // the map key. Using the key (object_id) here would never match
    // loaded_id, leaving objects_and_instances empty and causing
    // `orca-cli object list` to report every object as on an unnamed plate.
    for (auto& plate : s.plates) {
        plate->objects_and_instances.clear();
        for (const auto& kv : plate->obj_inst_map) {
            const int identify_id = kv.second.second;
            for (size_t oi = 0; oi < s.model->objects.size(); ++oi) {
                const ModelObject* obj = s.model->objects[oi];
                for (size_t ii = 0; ii < obj->instances.size(); ++ii) {
                    if (int(obj->instances[ii]->loaded_id) == identify_id) {
                        plate->objects_and_instances.emplace_back(int(oi), int(ii));
                    }
                }
            }
        }
    }

    return s;
}

void save_project(const ProjectState& s, const std::string& target_path)
{
    using namespace Slic3r;

    // Write to "<target>.tmp" then atomic-rename, so a crash mid-write does
    // not leave a half-written .3mf at the destination path.
    fs::path tmp = fs::path(target_path).string() + ".tmp";
    // store_bbs_3mf takes a `const char*` for the path. Bind the string to a
    // local so it outlives the StoreParams.
    std::string tmp_path = tmp.string();

    PlateDataPtrs plate_data = s.plate_data_ptrs();

    StoreParams sp;
    sp.path             = tmp_path.c_str();
    sp.model            = s.model.get();
    sp.plate_data_list  = plate_data;
    sp.config           = s.project_config.get();
    // Default in the struct already includes Zip64, which is what the GUI uses
    // when saving a regular project; do not change it without strong reason.
    sp.strategy         = SaveStrategy::Zip64;

    const bool ok = store_bbs_3mf(sp);
    if (!ok) {
        boost::system::error_code ec;
        fs::remove(tmp, ec);
        throw std::runtime_error("store_bbs_3mf failed: " + target_path);
    }

    // Carry through plate thumbnail blobs from the originating archive (if
    // any) before running the invariant guard. store_bbs_3mf always writes
    // relationships pointing at plate_<n>.png / plate_<n>_small.png but only
    // emits the file bodies when StoreParams::thumbnail_data is provided. For
    // a CLI that does not render, the realistic source of those bytes is the
    // input archive itself, and the clone-and-mutate flow ensures the source
    // remains valid.
    passthrough_missing_thumbnails(s, tmp_path);

    // Run runtime invariant guards on the .tmp BEFORE renaming over the
    // destination. If any check fails, remove the .tmp and propagate so the
    // CLI maps the failure to ExitCode::invariant_violation. The destination
    // is untouched on failure.
    try {
        run_all_invariants(s, tmp_path);
    } catch (const InvariantViolation&) {
        boost::system::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }

    // Safer atomic swap: rename the existing destination to .bak before
    // moving the .tmp in. This eliminates the window on Windows where the
    // destination doesn't exist between the remove() and rename() calls.
    // If the final rename fails we restore the original from .bak, and once
    // the .tmp is in place we drop the .bak (best-effort; a stale .bak is
    // harmless and will be cleaned up on the next successful save).
    boost::system::error_code ec;
    const std::string bak = target_path + ".bak";

    if (fs::exists(target_path)) {
        fs::rename(target_path, bak, ec);
        if (ec) {
            // Clean up the .tmp before bailing so we don't leave litter.
            boost::system::error_code rm_ec;
            fs::remove(tmp, rm_ec);
            throw std::runtime_error("save_project: rename existing -> .bak failed: " + ec.message());
        }
    }

    fs::rename(tmp, target_path, ec);
    if (ec) {
        // Best-effort restore: put the original back from .bak.
        boost::system::error_code rec;
        if (fs::exists(bak)) fs::rename(bak, target_path, rec);
        throw std::runtime_error("save_project: rename .tmp -> target failed: " + ec.message());
    }

    if (fs::exists(bak)) {
        boost::system::error_code rm_ec;
        fs::remove(bak, rm_ec);
        // Best-effort: if we can't remove the .bak, leave it; it will be
        // collected next time save_project runs over this target.
    }
}

std::string resolve_save_target(const GlobalOpts& opts, const std::string& input_file)
{
    return opts.output.has_value() ? *opts.output : input_file;
}

int check_input_exists(const GlobalOpts& g, const std::string& path)
{
    if (!fs::exists(path)) {
        print_err(g, ExitCode::file_not_found, "input not found: " + path);
        return int(ExitCode::file_not_found);
    }
    return int(ExitCode::ok);
}

} // namespace orca_cli

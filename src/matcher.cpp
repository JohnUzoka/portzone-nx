#include "matcher.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

#include "files.hpp"

namespace pz::matcher
{

namespace
{

    std::vector<std::string> split(const std::string& path)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < path.size())
        {
            size_t end = path.find('/', start);
            if (end == std::string::npos)
                end = path.size();
            if (end > start)
                parts.push_back(path.substr(start, end - start));
            start = end + 1;
        }
        return parts;
    }

    std::string join(const std::vector<std::string>& parts, size_t from, size_t to)
    {
        std::string out;
        for (size_t i = from; i < to; i++)
            out += (out.empty() ? "" : "/") + parts[i];
        return out;
    }

    std::string child(const std::string& dir, const std::string& name) { return dir.empty() ? name : dir + "/" + name; }

    bool isGlob(const std::string& part) { return part.find_first_of("*?") != std::string::npos; }

    bool nameMatches(const std::string& pattern, const std::string& name)
    {
        return isGlob(pattern) ? globMatch(pattern, name) : lower(pattern) == lower(name);
    }

    // The sources as folders and files, all keys lowercase.
    struct Tree
    {
        struct File
        {
            std::vector<std::string> parts; // as given
            std::string dir;                // lowercase parent path, "" at the top
        };
        struct Dir
        {
            std::vector<size_t> files;      // directly inside
            std::vector<size_t> all;        // anywhere below
            std::vector<std::string> dirs;  // lowercase paths of the folders directly inside
            std::vector<std::string> parts; // as given
        };

        std::vector<File> files; // parallel to the sources; junk has no parts
        std::map<std::string, Dir> dirs;
        std::unordered_map<std::string, size_t> byPath;                // lowercase path -> source
        std::unordered_map<std::string, std::vector<size_t>> byName;   // lowercase file name -> sources
        std::unordered_map<std::string, std::vector<size_t>> byExtension; // ".iso" -> sources

        explicit Tree(const std::vector<Source>& sources)
        {
            files.resize(sources.size());
            dirs[""];
            for (size_t i = 0; i < sources.size(); i++)
            {
                std::vector<std::string> parts = split(sources[i].path);
                if (parts.empty())
                    continue;
                bool junk = isJunk(parts.back());
                for (size_t p = 0; p + 1 < parts.size(); p++)
                    junk |= lower(parts[p]) == "__macosx";
                if (junk)
                    continue;

                std::string dir;
                dirs[""].all.push_back(i);
                for (size_t p = 0; p + 1 < parts.size(); p++)
                {
                    const std::string next = child(dir, lower(parts[p]));
                    auto [it, added]       = dirs.try_emplace(next);
                    if (added)
                    {
                        it->second.parts.assign(parts.begin(), parts.begin() + long(p) + 1);
                        dirs[dir].dirs.push_back(next);
                    }
                    it->second.all.push_back(i);
                    dir = next;
                }
                dirs[dir].files.push_back(i);

                const std::string name = lower(parts.back());
                byPath[child(dir, name)] = i;
                byName[name].push_back(i);
                const size_t dot = name.rfind('.');
                if (dot != std::string::npos)
                    byExtension[name.substr(dot)].push_back(i);
                files[i] = { std::move(parts), dir };
            }
        }

        // Files whose name matches `pattern`.
        std::vector<size_t> named(const std::string& pattern) const
        {
            const std::string p = lower(pattern);
            if (!isGlob(p))
            {
                const auto it = byName.find(p);
                return it == byName.end() ? std::vector<size_t>() : it->second;
            }
            // "*.iso" is the common glob: look it up by extension.
            if (p.size() > 2 && p[0] == '*' && p[1] == '.' && !isGlob(p.substr(1)))
            {
                const auto it = byExtension.find(p.substr(1));
                return it == byExtension.end() ? std::vector<size_t>() : it->second;
            }
            std::vector<size_t> out;
            for (size_t i = 0; i < files.size(); i++)
                if (!files[i].parts.empty() && globMatch(p, files[i].parts.back()))
                    out.push_back(i);
            return out;
        }

        // `relative` (a file or folder path, ignoring case; * and ? in the last part) under `dir`.
        bool has(const std::string& dir, const std::string& relative) const
        {
            const auto parts = split(relative);
            std::string at   = dir;
            for (size_t p = 0; p < parts.size(); p++)
            {
                const bool last = p + 1 == parts.size();
                if (!isGlob(parts[p]))
                {
                    const std::string next = child(at, lower(parts[p]));
                    if (last)
                        return byPath.count(next) || dirs.count(next);
                    if (!dirs.count(next))
                        return false;
                    at = next;
                    continue;
                }
                const Dir& d = dirs.at(at);
                for (size_t file : d.files)
                    if (globMatch(parts[p], files[file].parts.back()))
                        return true;
                for (const auto& sub : d.dirs)
                    if (globMatch(parts[p], dirs.at(sub).parts.back()))
                        return true;
                return false;
            }
            return true;
        }

        // The path `levels` folders above `dir` (lowercase).
        static std::string up(const std::string& dir, size_t levels)
        {
            std::string out = dir;
            for (size_t i = 0; i < levels; i++)
            {
                const size_t slash = out.rfind('/');
                out                = slash == std::string::npos ? "" : out.substr(0, slash);
            }
            return out;
        }
    };

    constexpr const char* FLOATING = "\x01"; // a folder found by its markers alone: any base fits

    // One way a file spec can be met.
    struct Binding
    {
        std::string base; // the option's folder in the sources (lowercase), or FLOATING
        std::vector<Placement> files;
        int score = 0;
    };

    std::string specFolder(const std::string& path) // "a/b/c.bin" -> "a/b/", "c.bin" -> ""
    {
        const size_t slash = path.rfind('/', path.size() - 2);
        return slash == std::string::npos ? "" : path.substr(0, slash + 1);
    }

    // A file spec: one binding per source file that fits. `parts` are the spec's path parts left after stripping.
    std::vector<Binding> bindFile(const Tree& tree, const std::vector<Source>& sources, const FileSpec& spec,
        const std::vector<std::string>& parts)
    {
        std::vector<Binding> out;
        const std::string& last = parts.back();
        for (size_t f : tree.named(last))
        {
            const Tree::File& file = tree.files[f];
            const size_t folders   = parts.size() - 1;
            if (file.parts.size() < folders + 1)
                continue;
            bool fits = true;
            for (size_t p = 0; p < folders && fits; p++)
                fits = lower(file.parts[file.parts.size() - 1 - folders + p]) == lower(parts[p]);
            if (!fits)
                continue;
            if (spec.size && sources[f].size != *spec.size)
                continue;

            Binding binding;
            binding.base = Tree::up(file.dir, folders);
            Placement placement;
            placement.source = f;
            placement.dest   = isGlob(last) ? specFolder(spec.path) + file.parts.back() : spec.path;
            placement.size   = spec.size;
            for (const auto& hash : spec.sha256)
                placement.sha256.push_back(lower(hash));
            binding.files.push_back(std::move(placement));
            binding.score = (isGlob(last) ? 2 : 4) + int(folders) + (spec.size ? 6 : 0);
            out.push_back(std::move(binding));
        }
        return out;
    }

    // A folder spec: one binding per folder that holds its markers (or a file matching `match`; without either, a
    // non-empty folder with the spec's name). Every file below it goes along (with `match`: the matching files in it).
    std::vector<Binding> bindFolder(const Tree& tree, const FileSpec& spec, const std::vector<std::string>& parts)
    {
        std::vector<Binding> out;
        const bool anyName = !spec.markers.empty() || !spec.match.empty();
        for (const auto& [path, dir] : tree.dirs)
        {
            if (path.empty() && !anyName)
                continue;
            // Whether the folder (and the folders above it) carry the spec's names: then it fixes the option's base.
            bool named = !dir.parts.empty() && dir.parts.size() >= parts.size();
            for (size_t p = 0; p < parts.size() && named; p++)
                named = nameMatches(parts[p], dir.parts[dir.parts.size() - parts.size() + p]);
            if (!named && !anyName)
                continue;

            bool fits = true;
            for (const auto& marker : spec.markers)
                fits = fits && tree.has(path, marker);
            std::vector<size_t> chosen;
            if (!spec.match.empty())
            {
                for (size_t f : dir.files)
                    if (globMatch(spec.match, tree.files[f].parts.back()))
                        chosen.push_back(f);
                fits = fits && !chosen.empty();
            }
            else
                chosen = dir.all;
            if (!fits || chosen.empty())
                continue;

            Binding binding;
            binding.base  = named ? Tree::up(path, parts.size()) : FLOATING;
            binding.score = 3 * int(spec.markers.size()) + (named ? 3 : 0) + (spec.match.empty() ? 0 : 2);
            const size_t depth = dir.parts.size();
            for (size_t f : chosen)
            {
                const auto& fileParts = tree.files[f].parts;
                Placement placement;
                placement.source = f;
                placement.dest   = spec.path + join(fileParts, depth, fileParts.size());
                binding.files.push_back(std::move(placement));
            }
            out.push_back(std::move(binding));
        }
        return out;
    }

    struct Candidate
    {
        std::string optionLabel;
        std::vector<Placement> files;
        int score = 0;
    };

    // Every way `option` is met by the sources.
    std::vector<Candidate> bindOption(const Tree& tree, const std::vector<Source>& sources, const Option& option)
    {
        std::vector<std::vector<std::string>> paths;
        for (const auto& spec : option.files)
            paths.push_back(split(spec.path));
        // Leading folders every spec shares may be missing in the sources (import/code.bin met by oot/code.bin).
        size_t shared = 0;
        for (;; shared++)
        {
            bool same = true;
            for (const auto& p : paths)
                same = same && p.size() > shared + 1 && lower(p[shared]) == lower(paths[0][shared]);
            if (!same)
                break;
        }

        std::vector<Candidate> out;
        for (size_t strip = 0; strip <= shared; strip++)
        {
            std::vector<std::vector<Binding>> perSpec;
            for (size_t s = 0; s < option.files.size(); s++)
            {
                const FileSpec& spec = option.files[s];
                const std::vector<std::string> parts(paths[s].begin() + long(strip), paths[s].end());
                perSpec.push_back(spec.isFolder() ? bindFolder(tree, spec, parts) : bindFile(tree, sources, spec, parts));
                if (perSpec.back().empty())
                    break;
            }
            if (perSpec.size() < option.files.size() || perSpec.back().empty())
                continue;

            std::set<std::string> bases;
            for (const auto& bindings : perSpec)
                for (const auto& binding : bindings)
                    bases.insert(binding.base);
            for (const auto& base : bases)
            {
                // Per spec, the bindings that fit this base; the spec with the most choices spreads into candidates.
                std::vector<std::vector<const Binding*>> fitting;
                size_t widest = 0;
                for (const auto& bindings : perSpec)
                {
                    fitting.emplace_back();
                    for (const auto& binding : bindings)
                        if (binding.base == base || binding.base == FLOATING)
                            fitting.back().push_back(&binding);
                    if (fitting.back().size() > fitting[widest].size())
                        widest = fitting.size() - 1;
                }
                if (std::any_of(fitting.begin(), fitting.end(), [](const auto& f) { return f.empty(); }))
                    continue;
                for (const Binding* spread : fitting[widest])
                {
                    Candidate candidate { option.label, {}, strip == 0 ? 1 : 0 };
                    for (size_t s = 0; s < fitting.size(); s++)
                    {
                        const Binding* pick = s == widest ? spread :
                            *std::max_element(fitting[s].begin(), fitting[s].end(),
                                [](const Binding* a, const Binding* b) { return a->score < b->score; });
                        candidate.files.insert(candidate.files.end(), pick->files.begin(), pick->files.end());
                        candidate.score += pick->score;
                    }
                    out.push_back(std::move(candidate));
                }
            }
        }
        return out;
    }

    std::set<size_t> sourcesOf(const Match& match)
    {
        std::set<size_t> out;
        for (const auto& file : match.files)
            out.insert(file.source);
        return out;
    }

    // Union-find over match indexes.
    size_t root(std::vector<size_t>& parent, size_t i)
    {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    }

} // namespace

bool isJunk(const std::string& name)
{
    const std::string n = lower(name);
    return n == "thumbs.db" || n == ".ds_store" || n == "desktop.ini" || n.rfind("._", 0) == 0 || n == ".localized";
}

Plan plan(const std::vector<Source>& sources, const std::vector<const Port*>& ports, const std::string& onlyInput)
{
    const Tree tree(sources);
    Plan result;
    std::vector<size_t> ambiguity; // per match: the input's tie group (index into result.matches of its first member)

    for (const Port* port : ports)
    {
        if (!port->manifest || !port->manifest->gameData)
            continue;
        const GameData& data  = *port->manifest->gameData;
        const DataCheck check = checkGameData(data);
        for (size_t i = 0; i < data.inputs.size(); i++)
        {
            const Input& input = data.inputs[i];
            if (!onlyInput.empty() && input.id != onlyInput)
                continue;

            std::vector<Candidate> candidates;
            for (const auto& option : input.options)
                for (auto& candidate : bindOption(tree, sources, option))
                    candidates.push_back(std::move(candidate));
            if (candidates.empty())
                continue;

            // The best way to meet the input; several equally good ones with different files are the player's call.
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
            std::vector<std::set<size_t>> seen;
            const size_t first = result.matches.size();
            for (auto& candidate : candidates)
            {
                if (candidate.score < candidates.front().score)
                    break;
                std::set<size_t> used;
                for (const auto& file : candidate.files)
                    used.insert(file.source);
                if (std::find(seen.begin(), seen.end(), used) != seen.end())
                    continue;
                seen.push_back(used);

                Match match;
                match.portId      = port->info.id;
                match.portTitle   = port->info.title;
                match.gameDir     = data.dir;
                match.inputId     = input.id;
                match.inputLabel  = input.label;
                match.optionLabel = candidate.optionLabel;
                match.required    = input.required;
                match.alreadyMet  = check.inputs[i].met;
                match.files       = std::move(candidate.files);
                match.score       = candidate.score;
                for (const auto& file : match.files)
                    match.bytes += sources[file.source].size;
                std::sort(match.files.begin(), match.files.end(), [](const Placement& a, const Placement& b) { return a.dest < b.dest; });
                result.matches.push_back(std::move(match));
                ambiguity.push_back(first);
            }
        }
    }

    // Alternatives: ties within an input, and matches that want the same source file.
    const size_t n = result.matches.size();
    std::vector<size_t> parent(n);
    for (size_t i = 0; i < n; i++)
        parent[i] = i;
    for (size_t i = 0; i < n; i++)
        parent[root(parent, i)] = root(parent, ambiguity[i]);
    std::vector<std::set<size_t>> used(n);
    for (size_t i = 0; i < n; i++)
        used[i] = sourcesOf(result.matches[i]);
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (std::any_of(used[i].begin(), used[i].end(), [&](size_t s) { return used[j].count(s) > 0; }))
                parent[root(parent, i)] = root(parent, j);

    std::map<size_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < n; i++)
        groups[root(parent, i)].push_back(i);
    int nextGroup = 0;
    for (const auto& [_, members] : groups)
    {
        if (members.size() == 1)
        {
            Match& only  = result.matches[members[0]];
            only.selected = !only.alreadyMet || ports.size() == 1;
            continue;
        }
        // One clearly better member (markers, sizes, exact names) is suggested; a tie is left to the player.
        size_t best = members[0];
        int runnerUp = -1;
        for (size_t m : members)
            if (result.matches[m].score > result.matches[best].score)
                best = m;
        for (size_t m : members)
            if (m != best)
                runnerUp = std::max(runnerUp, result.matches[m].score);
        for (size_t m : members)
            result.matches[m].group = nextGroup;
        if (result.matches[best].score > runnerUp && (!result.matches[best].alreadyMet || ports.size() == 1))
            result.matches[best].selected = true;
        nextGroup++;
    }

    std::set<size_t> anyUsed;
    for (const auto& u : used)
        anyUsed.insert(u.begin(), u.end());
    result.unused = sources.size() - anyUsed.size();
    return result;
}

} // namespace pz::matcher

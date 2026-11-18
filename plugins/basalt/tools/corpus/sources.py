"""Basalt's corpus sources: the archives, what each fetch target reads from them, and the full
tier's manifest.

    python3 -I sources.py targets                 every fetch target, one per line
    python3 -I sources.py full-tier               the targets `all` stands for
    python3 -I sources.py plan <target> <cache>   one line per archive: key, url, dest, globs (tabs)
    python3 -I sources.py check <archive>         the host still publishes the pinned MD5
    python3 -I sources.py manifest <cache>        <cache>/manifest.tsv over everything fetched

Every source is CC BY 4.0 and sits in a zip on Zenodo or Edinburgh DataShare. fetch.sh reads only
the members it needs with HTTP range requests (remotezip.py), and zipfile checks each member's
CRC-32 against the archive's central directory. The MD5 pinned per archive is the one its host
publishes; `check` asks the host again before every fetch, so a replaced archive stops the run.
"""

import json
import os
import sys
import urllib.request

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import audio  # noqa: E402
USER_AGENT = "myzel-corpus-fetch/1"


class Archive:
    def __init__(self, source, folder, host, record, name, md5):
        self.source, self.folder, self.host, self.record, self.name, self.md5 = (
            source, folder, host, record, name, md5)

    @property
    def url(self):
        if self.host == "zenodo":
            return f"https://zenodo.org/api/records/{self.record}/files/{self.name}/content"
        return f"https://datashare.ed.ac.uk/server/api/core/bitstreams/{self.record}/content"

    def published_md5(self):
        if self.host == "zenodo":
            record = fetch_json(f"https://zenodo.org/api/records/{self.record}")
            entry = next(f for f in record["files"] if f["key"] == self.name)
            algorithm, value = entry["checksum"].split(":", 1)
        else:
            entry = fetch_json(f"https://datashare.ed.ac.uk/server/api/core/bitstreams/{self.record}")
            algorithm, value = entry["checkSum"]["checkSumAlgorithm"], entry["checkSum"]["value"]
        if algorithm.lower() != "md5":
            raise OSError(f"{self.name}: the host publishes {algorithm}, not MD5")
        return value


ARCHIVES = {    # key: source (ATTRIBUTION.md), cache folder, host, record, file, the host's MD5
    "albumdb-raw": Archive("AlbumDB", "albumdb", "zenodo", "19683001", "stems_raw.zip",
                           "d38a9539f4a88559f4d7ef61c9b856d1"),
    "albumdb-masters": Archive("AlbumDB", "albumdb", "zenodo", "19683001", "masters_stereo.zip",
                               "06ff09dfe1d03788c71071b419eb0366"),
    "guitar-techs-p3": Archive("Guitar-TECHS", "guitar-techs", "zenodo", "14963133", "P3_music.zip",
                               "071ba80aecf00f4a31fbd167b3f22198"),
    "guitarset-mic": Archive("GuitarSet", "guitarset", "zenodo", "3371780", "audio_mono-mic.zip",
                             "275966d6610ac34999b58426beb119c3"),
    "vctk": Archive("VCTK", "vctk", "datashare", "535f4286-e54c-4038-838c-a02285e32cb2",
                    "VCTK-Corpus-0.92.zip", "8a6ba2946b36fcbef0212cad601f4bfa"),
}

# One raw stem per role and song where the song has it; the names are AlbumDB's.
ALBUMDB_STEMS = ("drums_kick", "drums_snare", "drums_snare_1", "drums_overheads", "bass", "vox_lead",
                 "keys_piano", "guitar_acoustic", "guitar_acoustic_main", "guitar_acoustic_l",
                 "guitar_electric_rhythm", "guitar_electric_rhythm_l")
# Two women, two men, four accents; none is in Halite's committed tier.
VCTK_SPEAKERS = ("p228", "p232", "p335", "p326")

TARGETS = {
    "albumdb-mixes": [("albumdb-masters", ["masters_stereo/*.wav"])],
    "albumdb-stems": [("albumdb-raw", [f"stems_raw/*/{stem}.wav" for stem in ALBUMDB_STEMS])],
    "guitar-techs": [("guitar-techs-p3", ["P3_music/audio/directinput/*.wav",
                                          "P3_music/audio/micamp/*.wav"])],
    "guitarset": [("guitarset-mic", ["*_comp_mic.wav"])],
    "vctk": [("vctk", ["speaker-info.txt"]
              + [f"wav48_silence_trimmed/{s}/{s}_*_mic2.flac" for s in VCTK_SPEAKERS])],
}
FULL_TIER = tuple(TARGETS)


def fetch_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def plan(target):
    """(archive key, globs) pairs; candidates and excerpts come from make_excerpts.py."""
    if target in TARGETS:
        return TARGETS[target]
    import make_excerpts
    if target == "candidates":
        return make_excerpts.candidate_globs()
    if target == "excerpts":
        return make_excerpts.excerpt_members()
    raise KeyError(target)


def category(archive, member):
    name = os.path.basename(member).rsplit(".", 1)[0]
    if archive == "albumdb-masters":
        return "mix"
    if archive == "albumdb-raw":
        for prefix, role in (("drums_kick", "kick"), ("drums_snare", "snare"),
                             ("drums_overheads", "overheads"), ("bass", "bass"),
                             ("vox_lead", "vocal"), ("keys_piano", "piano"),
                             ("guitar_acoustic", "acoustic-guitar"),
                             ("guitar_electric", "electric-guitar")):
            if name.startswith(prefix):
                return role
    if archive == "guitar-techs-p3":
        return "electric-guitar-di" if "/directinput/" in member else "electric-guitar-amp"
    if archive == "guitarset-mic":
        return "acoustic-guitar"
    if archive == "vctk" and member.endswith(".flac"):
        return "speech"
    return None


def manifest(cache):
    """Every fetched recording with a category: id, category, source, path, rate, channels, bits, seconds."""
    rows = []
    for key, archive in ARCHIVES.items():
        root = os.path.join(cache, archive.folder)
        for dirpath, _, files in sorted(os.walk(root)):
            for name in sorted(files):
                path = os.path.join(dirpath, name)
                member = os.path.relpath(path, root)
                role = category(key, member) if not name.endswith(".part") else None
                if role is None or not archive_holds(key, member):
                    continue
                rate, channels, bits, seconds = audio.probe(path)
                item = f"{archive.folder}/{member.rsplit('.', 1)[0]}"
                rows.append((item, role, archive.source, os.path.relpath(path, cache),
                             str(rate), str(channels), str(bits), f"{seconds:.3f}"))
    target = os.path.join(cache, "manifest.tsv")
    with open(target + ".part", "w", encoding="utf-8") as f:
        f.write("id\tcategory\tsource\tpath\trate\tchannels\tbits\tseconds\n")
        for row in rows:
            f.write("\t".join(row) + "\n")
    os.replace(target + ".part", target)
    print(f"  manifest: {len(rows)} recordings in {target}")


def archive_holds(key, member):
    """Two archives share AlbumDB's folder; each member belongs to the one its top folder names."""
    top = member.split("/", 1)[0]
    if key == "albumdb-raw":
        return top == "stems_raw"
    if key == "albumdb-masters":
        return top == "masters_stereo"
    return True


def main(argv):
    if argv[:1] == ["targets"]:
        print("\n".join(FULL_TIER + ("candidates", "excerpts")))
        return 0
    if argv[:1] == ["full-tier"]:
        print("\n".join(FULL_TIER))
        return 0
    if len(argv) == 3 and argv[0] == "plan":
        try:
            entries = plan(argv[1])
        except KeyError:
            print(f"Unknown target '{argv[1]}'.", file=sys.stderr)
            return 2
        for key, globs in entries:
            archive = ARCHIVES[key]
            print("\t".join([key, archive.url, os.path.join(argv[2], archive.folder), *globs]))
        return 0
    if len(argv) == 2 and argv[0] == "check":
        archive = ARCHIVES[argv[1]]
        published = archive.published_md5()
        if published != archive.md5:
            print(f"  {archive.name}: the host now publishes MD5 {published}, pinned {archive.md5}; "
                  "the archive changed, so the pins and excerpts need review", file=sys.stderr)
            return 1
        print(f"  {archive.name}: MD5 {published} as pinned")
        return 0
    if len(argv) == 2 and argv[0] == "manifest":
        manifest(argv[1])
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

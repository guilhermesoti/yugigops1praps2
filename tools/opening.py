"""Prepare the US intro from raw CD sectors for make OPENING=1."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from fm.disc import DiscImage
from preparar import data_track


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disc', type=Path)
    args = parser.parse_args()
    source = data_track(args.disc) if args.disc.suffix.lower() == '.cue' else args.disc
    build = Path(__file__).resolve().parents[1] / 'build'
    build.mkdir(exist_ok=True)
    with DiscImage(source) as disc, tempfile.TemporaryDirectory(dir=build) as temporary:
        entry = disc.find('DATA/MOVIE.STR')
        if not entry or disc.sector != 2352 or entry[1] != 35430400:
            raise ValueError('Use o BIN/CUE americano com setores de 2352 bytes.')
        disc.file.seek(entry[0] * 2352)
        raw = Path(temporary) / 'opening.str'
        # The US MOVIE.STR contains five clips of 346 frames each.
        raw.write_bytes(disc.file.read(3460 * 2352))
        video = raw.with_suffix('.mjpg')
        sound = raw.with_suffix('.pcm')
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'psxstr', '-i', str(raw),
                        '-map', '0:v:0', '-fps_mode', 'passthrough', '-c:v', 'mjpeg',
                        '-q:v', '4', '-f', 'mjpeg', str(video),
                        '-map', '0:a:0', '-ar', '24000', '-ac', '2', '-f', 's16le', str(sound)], check=True)
        pictures = video.read_bytes()
        if pictures.count(b'\xff\xd8') != 346 or pictures.count(b'\xff\xd9') != 346 or sound.stat().st_size != 2211840:
            raise ValueError('Abertura incompleta.')
        video.replace(build / 'opening.mjpg')
        sound.replace(build / 'opening.pcm')


if __name__ == '__main__':
    main()

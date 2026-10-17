"""Read-only access to an AmigaDOS floppy image via amitools."""
from amitools.fs.ADFSVolume import ADFSVolume
from amitools.fs.FSString import FSString
from amitools.fs.blkdev.BlkDevFactory import BlkDevFactory


class AdfError(Exception):
    pass


class Adf:
    def __init__(self, path):
        try:
            self.blkdev = BlkDevFactory().open(str(path), read_only=True)
        except Exception as e:  # amitools raises assorted types
            raise AdfError(f"{path} is not a readable ADF disk image: {e}")
        self.vol = ADFSVolume(self.blkdev)
        try:
            self.vol.open()
        except Exception as e:
            self.blkdev.close()
            raise AdfError(f"{path} does not contain an AmigaDOS filesystem: {e}")

    def read(self, path: str) -> bytes:
        node = self.vol.get_path_name(FSString(path))
        if node is None or not node.is_file():
            raise AdfError(f"file {path} not found on the disk")
        return node.get_file_data()

    def listdir(self, path: str):
        """File names in on-disk directory order (hash table order, not alphabetical).
        sounds/index.txt relies on this order, so do not sort."""
        node = self.vol.get_path_name(FSString(path))
        if node is None or node.is_file():
            raise AdfError(f"directory {path} not found on the disk")
        return [e.get_file_name().get_unicode_name() for e in node.get_entries()
                if e.is_file()]

    def close(self):
        self.vol.close()
        self.blkdev.close()

# Embedded Python Runtime

This project uses only bundled Python at runtime.
System-level Python is intentionally not used.

## Directory layout

```text
PythonModules/
  Scripts/
    WAVExport.py
    WAVImport.py
    PDFExport.py
    shared_paths.py
  Runtime/
    Python-3.14.3-embed-amd64/
      python.exe
      python314.dll
      python314.zip
      python314._pth
```

`python314._pth` must contain:

```text
..\..\Scripts
```

so embedded Python can import project scripts/modules.

## Recommended version

- Python `3.14.3` embeddable package (64-bit), as checked on 2026-03-31.

## Quick setup

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\PythonModules\Runtime\Fetch-EmbeddedPython.ps1
```

The script downloads and extracts:

- `https://www.python.org/ftp/python/3.14.3/python-3.14.3-embed-amd64.zip`

to:

- `PythonModules/Runtime/Python-3.14.3-embed-amd64/`

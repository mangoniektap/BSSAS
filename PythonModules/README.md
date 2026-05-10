# PythonModules

```text
PythonModules/
  Scripts/   # project Python scripts called by C++ backend
  Runtime/   # embedded Python runtimes bundled with installer
```

The application always uses embedded Python from `Runtime`.
It does not fall back to system Python.

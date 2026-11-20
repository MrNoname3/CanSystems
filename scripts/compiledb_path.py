"""Writes the environment's compilation database into its build directory.

`pio run -t compiledb` writes compile_commands.json into the project root by default, where each
environment would overwrite the last; scripts/analysis_check.py reads every one from here.
"""

Import("env")

env.Replace(COMPILATIONDB_PATH="$BUILD_DIR/compile_commands.json")

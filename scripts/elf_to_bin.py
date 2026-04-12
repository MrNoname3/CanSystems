Import("env")

# Function to convert .elf to .bin
def convert_elf_to_bin(source, target, env):
    # Custom BIN from ELF
    ret = env.Execute(" ".join([
        "$OBJCOPY", "-O", "binary",
        "$BUILD_DIR/${PROGNAME}.elf", "$BUILD_DIR/${PROGNAME}.bin"
    ]))
    if ret != 0:
        raise RuntimeError(f"objcopy failed with exit code {ret}")
    print("BIN file created successfully.")

# Add post-action to trigger the conversion after the ELF file is built
env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(convert_elf_to_bin, "Converting $BUILD_DIR/${PROGNAME}.elf to BIN")
)

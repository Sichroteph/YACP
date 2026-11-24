"""Enable application LTO for the Standard firmware's existing OTA partition."""
Import("env")

# PioArduino does not always forward the application flag to the link driver,
# particularly for S3 profiles that keep the prebuilt USB-capable SDK.
env.AppendUnique(LINKFLAGS=["-flto=auto"])

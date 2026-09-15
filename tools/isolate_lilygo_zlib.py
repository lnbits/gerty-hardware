"""Keep LilyGO's full zlib separate from PNGdec's preallocated inflater."""
Import("env")


def isolate_lilygo_zlib(build_env, node):
    isolated = build_env.Clone()
    # z_errmsg is an internal global that upstream Z_PREFIX does not rename.
    isolated.Append(CPPDEFINES=["Z_PREFIX", ("z_errmsg", "lilygo_z_errmsg")])
    return isolated.Object(node)


# Scope the prefix to the font renderer and its zlib, never PNGdec or the app.
env.AddBuildMiddleware(isolate_lilygo_zlib, "*/LilyGo-EPD47/src/*")

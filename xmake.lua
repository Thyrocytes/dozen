add_rules("mode.debug", "mode.release")

target("dozenc")
    set_kind("binary")
    set_languages("clatest")
    add_includedirs("include")
    add_syslinks("uring", "pthread")
    add_files("src/**.c")
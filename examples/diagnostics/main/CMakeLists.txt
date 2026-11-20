idf_component_register(SRCS "main.c" INCLUDE_DIRS "." PRIV_REQUIRES candis_example_usb)
if(CONFIG_CANDIS_DIAGNOSTICS_AUDIO)
    target_sources(${COMPONENT_LIB} PRIVATE "audio.c")
endif()

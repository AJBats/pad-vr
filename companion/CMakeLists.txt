add_executable(padvr_companion
    main.cpp
)

target_compile_definitions(padvr_companion PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(padvr_companion PRIVATE
    XInput
)

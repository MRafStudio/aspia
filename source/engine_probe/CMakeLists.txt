#
# RadNET probe (fork-local).
#
# Собирает aspia_probe.dll: цикл событий Qt (QCoreApplication + AsioEventDispatcher)
# за плоским C-ABI. Нужен, чтобы проверить загрузку движка из .NET до того, как
# резать настоящие движки.
#
add_library(aspia_probe SHARED
    probe.cc
    probe.h
)

target_compile_definitions(aspia_probe PRIVATE ASPIA_PROBE_EXPORTS)

find_package(asio CONFIG REQUIRED)

target_link_libraries(aspia_probe PRIVATE
    aspia_base
    # aspia_base линкует Qt и asio как PRIVATE, поэтому include-пути сюда не наследуются.
    Qt6::Core
    asio::asio
)

set_target_properties(aspia_probe PROPERTIES
    OUTPUT_NAME "aspia_probe"
    PREFIX ""
)

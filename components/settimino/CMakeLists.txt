idf_component_register(SRCS "Settimino.cpp" "idf_client.cpp" INCLUDE_DIRS "." REQUIRES lwip esp_timer freertos)
# Upstream has unused locals in methods outside this diagnostic.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)

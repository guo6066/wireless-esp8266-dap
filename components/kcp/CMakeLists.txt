set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_SRCS "./ikcp.c ./ikcp_util.c ")
set(COMPONENT_REQUIRES esp_http_server http_parser spi_flash)
register_component()

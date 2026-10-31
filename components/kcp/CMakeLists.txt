set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_SRCS "./ikcp.c ./ikcp_util.c ./lfs_driver.c ./ota.c ./ofs.c")
set(COMPONENT_REQUIRES esp_http_server http_parser)
register_component()

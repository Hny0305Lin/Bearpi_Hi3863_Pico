set(project_name ws63)
add_path_with_alias_if_exist(${WIFI_DIR}/source/alg wifi_alg_host)
# ���ws63������alg����
set(
    WIFI_ALG_WS63_SOURCES
#add alg src file
)
set(
    WIFI_ALG_WS63_INCLUDES
#add alg inc file
)
set(WIFI_ALG_WS63_DEFINES
#add alg defines
    _PRE_WLAN_FEATURE_TEMP_PROTECT_CLDO_MODE
)
set_property(GLOBAL PROPERTY WIFI_ALG_WS63_SOURCES ${WIFI_ALG_WS63_SOURCES})
set_property(GLOBAL PROPERTY WIFI_ALG_WS63_INCLUDES ${WIFI_ALG_WS63_INCLUDES})
set_property(GLOBAL PROPERTY WIFI_ALG_WS63_DEFINES ${WIFI_ALG_WS63_DEFINES})

set(ws63_alg_ind_wifi
    WIFI_ALG_IOT_HOST_IND_SOURCES
    WIFI_ALG_IOT_DEVICE_IND_SOURCES
)

set(ws63_alg_wifi_host_ram
    WIFI_SOURCES # �ָ���
# ��� src ���
    WIFI_ALG_COMMON_HOST_SOURCES
    WIFI_ALG_IOT_HOST_SOURCES
    WIFI_ALG_WS63_SOURCES
    WIFI_INCLUDES # �ָ���
# ��� inc ���
    WIFI_ALG_COMMON_INCLUDES
    WIFI_ALG_IOT_HOST_INCLUDES
    WIFI_ALG_WS63_INCLUDES
    WIFI_DEFINES # �ָ���
# ��� defines ���
    WIFI_ALG_WS63_DEFINES
    WIFI_ALG_IOT_DEFINES
    WIFI_ALG_IOT_HMAC_DEFINES
)

set(ws63_alg_wifi_tcm
    WIFI_SOURCES # �ָ���
# ��� src ���

    WIFI_INCLUDES # �ָ���
# ��� inc ���
    WIFI_ALG_COMMON_INCLUDES
    WIFI_ALG_IOT_HOST_INCLUDES
    WIFI_ALG_IOT_DEVICE_ROM_INCLUDES
    WIFI_ALG_IOT_DEVICE_RAM_INCLUDES
    WIFI_ALG_WS63_INCLUDES
    WIFI_DEFINES # �ָ���
# ��� defines ���
    WIFI_ALG_WS63_DEFINES
    WIFI_ALG_IOT_DEFINES
    WIFI_ALG_IOT_HMAC_DEFINES
)
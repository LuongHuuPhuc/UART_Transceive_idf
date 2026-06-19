file(GLOB UART_SOURCES
  "*.c"
)

idf_component_register(
  SRCS
    ${UART_SOURCES}
    
  INCLUDE_DIRS 
    "include"

  REQUIRES 
    freertos driver common_helper sensor_packet
 
  PRIV_REQUIRES 
    # Wireless_network_test components
    common_cfg bluetooth_cfg
)
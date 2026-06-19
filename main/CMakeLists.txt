idf_component_register(
  SRCS 
    "main.c"
  
  INCLUDE_DIRS 
    "."

  REQUIRES
    freertos uart_proc common_helper

  PRIV_REQUIRES 
    # Wireless_network_test components
    common_cfg bluetooth_cfg
)
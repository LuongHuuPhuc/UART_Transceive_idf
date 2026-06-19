idf_component_register(
  SRCS 
    "sensor_pkt.c"
    
  INCLUDE_DIRS 
    "include"

  REQUIRES 
    freertos driver common_helper
)
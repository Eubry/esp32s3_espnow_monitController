idf_component_register(SRCS "main/main.cpp"
                    PRIV_REQUIRES 
                        nvs_flash 
                        esp_event 
                        esp_netif 
                        esp_wifi
                    INCLUDE_DIRS "."
                    "lib/main"
                    )

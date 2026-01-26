/** @file akira_api.c
 * @brief Implementation of the Akira API for the AKIRA SDK. 
 * 
 * This file provides functions to interact with GPIO pins, timers, sensors, and messaging.
 * It includes functions to configure GPIO pins, set and get their states, register callbacks,
 * and handle events from the runtime.
 * 
*/

#include "akira_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Configuration constants */
#define MAX_EVENTS_PER_CALL 5
#define IDLE_SLEEP_MS 100

static void (*timer_callbacks[AKIRA_MAX_CALLBACKS])(void) = {0};
static void (*gpio_callbacks[AKIRA_MAX_CALLBACKS])(void) = {0};
static message_callback_func_t message_callbacks[AKIRA_MAX_CALLBACKS] = {0};
static char message_callback_topics[AKIRA_MAX_CALLBACKS][AKIRA_MAX_TOPIC_LEN] = {{0}};
static int gpio_callback_pins[AKIRA_MAX_CALLBACKS] = {-1};
static int gpio_callback_ports[AKIRA_MAX_CALLBACKS] = {-1};

static void initialize_callback(){
    static bool initialized = false;
    if (!initialized)
    {
        for(int i = 0; i < AKIRA_MAX_CALLBACKS; i++){
            timer_callbacks[i] = NULL;
            gpio_callbacks[i] = NULL;
            message_callbacks[i] = NULL;
            memset(message_callback_topics[i], 0, AKIRA_MAX_TOPIC_LEN);
            gpio_callback_pins[i] = -1;
            gpio_callback_ports[i] = -1;
        }
        initialized = true;
    }
}

/*===========================================================================*/
/* Dispatch Callbacks                                                        */
/*===========================================================================*/

/**
 * @brief Dispatch timer event to registered callback
 * @param timer_id Timer identifier
 */
static void dispatch_timer_event(uint32_t timer_id) {
    initialize_callback();
    
    if (timer_id >= AKIRA_MAX_CALLBACKS) {
        printf("Timer event with invalid ID: %u (max: %d)\n", 
               timer_id, AKIRA_MAX_CALLBACKS - 1);
        return;
    }
    
    timer_callback_func_t callback = timer_callbacks[timer_id];
    
    if (callback != NULL) {
        callback();
    } else {
        printf("Timer %u fired but no callback is registered\n", timer_id);
    }
}

/**
 * @brief Dispatch GPIO event to registered callback
 * @param pin GPIO pin number
 * @param port GPIO port number
 * @param state New pin state
 */
static void dispatch_gpio_event(uint8_t pin, uint8_t port, uint8_t state) {
    initialize_callback();
    
    for (int i = 0; i < AKIRA_MAX_CALLBACKS; i++) {
        if (gpio_callback_pins[i] == pin && gpio_callback_ports[i] == port) {
            gpio_callback_func_t callback = gpio_callbacks[i];
            
            if (callback != NULL) {
                callback(state);
                return;
            }
        }
    }
    
    printf("GPIO event on pin %u, port %u (state=%u) but no callback registered\n",
           pin, port, state);
}

/**
 * @brief Dispatch message event to all registered callbacks for the topic
 * @param topic Message topic
 * @param content_type Content type string
 * @param payload Message payload
 * @param payload_len Payload length in bytes
 */
static void dispatch_message_event(const char *topic, 
                                   const char *content_type,
                                   const void *payload, 
                                   uint32_t payload_len) {
    initialize_callback();
    
    if (topic == NULL || topic[0] == '\0') {
        printf("Message event received with invalid topic\n");
        return;
    }
    
    int callbacks_invoked = 0;
    
    for (int i = 0; i < AKIRA_MAX_CALLBACKS; i++) {
        if (message_callbacks[i] == NULL) {
            continue;
        }
        
        if (strcmp(message_callback_topics[i], topic) == 0) {
            message_callbacks[i](topic, content_type, payload, payload_len);
            callbacks_invoked++;
        }
    }
    
    if (callbacks_invoked == 0) {
        printf("Message received on topic '%s' but no callbacks handled it\n", topic);
    }
}

/*===========================================================================*/
/* Register callbacks                                                        */
/*===========================================================================*/

int akira_register_timer_callback(int timer_id, timer_callback_func_t callback){

    initialize_callback();
    if(timer_id < 0 || timer_id >= AKIRA_MAX_TIMERS){
        printf("Invalid timer ID %d\n", timer_id);
        return -EINVAL;
    }
    if(!callback){
        printf("Invalid timer callback for ID %d\n", timer_id);
        return -EINVAL;
    }

    // Check for duplicate registration
    if(timer_callbacks[timer_id] != NULL){
        printf("Warning: Timer %d already has a callback registered, overwriting\n", timer_id);
    }

    timer_callbacks[timer_id] = callback;
    printf("Registered timer callback for ID %d\n", timer_id);
    return 0;
}

int akira_register_gpio_callback(gpio_callback_func_t callback, int port, int pin){
    initialize_callback();
    if(!callback){
        printf("Invalid gpio callback for pin %d and port %d\n", pin, port);
        return -EINVAL;
    }
    if(pin < 0 || pin >= AKIRA_MAX_GPIO_PINS_PER_PORT || port < 0 || port >= AKIRA_MAX_GPIO_PORTS){
        printf("Invalid gpio pin %d or port %d\n", pin, port);
        return -EINVAL;
    }
    
    // Check for duplicate registration
    for(int i = 0; i < AKIRA_MAX_CALLBACKS; i++){
        if(gpio_callback_pins[i] == pin && gpio_callback_ports[i] == port){
            printf("Warning: GPIO pin %d, port %d already has a callback registered, overwriting\n", pin, port);
            gpio_callbacks[i] = callback;
            return 0;
        }
    }
    
    // Find empty slot
    for(int i = 0; i < AKIRA_MAX_CALLBACKS; i++){
        if(gpio_callbacks[i] == NULL){
            gpio_callbacks[i] = callback;
            gpio_callback_ports[i] = port;
            gpio_callback_pins[i] = pin;
            printf("Registered GPIO callback for pin %d, port %d\n", pin, port);
            return 0;
        }
    }
    printf("Failed to register gpio callback for pin %d and port %d - no slots available\n", pin, port);
    return -ENOMEM;
}

int akira_register_message_callback(message_callback_func_t callback, const char *topic){
    initialize_callback();
    if(!topic || topic[0] == '\0'){
        printf("Topic is null or empty\n");
        return -EINVAL;
    }
    if(!callback){
        printf("Invalid message callback for topic %s\n", topic);
        return -EINVAL;
    }
    
    // Validate topic length before copying
    size_t topic_len = strlen(topic);
    if(topic_len >= AKIRA_MAX_TOPIC_LEN){
        printf("Topic '%s' is too long (%zu bytes, max %d)\n", topic, topic_len, AKIRA_MAX_TOPIC_LEN - 1);
        return -EINVAL;
    }
    
    // Check for duplicate registration
    for(int i = 0; i < AKIRA_MAX_CALLBACKS; i++){
        if(message_callback_topics[i][0] != '\0' && strcmp(message_callback_topics[i], topic) == 0){
            printf("Warning: Topic '%s' already has a callback registered, overwriting\n", topic);
            message_callbacks[i] = callback;
            return 0;
        }
    }

    // Find empty slot
    for(int i = 0; i < AKIRA_MAX_CALLBACKS; i++){
        if(message_callback_topics[i][0] == '\0'){
            message_callbacks[i] = callback;
            strncpy(message_callback_topics[i], topic, AKIRA_MAX_TOPIC_LEN - 1);
            message_callback_topics[i][AKIRA_MAX_TOPIC_LEN - 1] = '\0'; 
            printf("Registered message callback for topic '%s'\n", topic);
            return 0;
        }
    }
    printf("Failed to register message callback for topic '%s' - no slots available\n", topic);
    return -ENOMEM;
}

/*===========================================================================*/
/* Unregister callbacks                                                      */
/*===========================================================================*/

int akira_unregister_timer_callback(int timer_id) {
    initialize_callback();
    
    if(timer_id < 0 || timer_id >= AKIRA_MAX_TIMERS) {
        printf("Error: Timer ID %d out of range (0-%d)\n", 
               timer_id, AKIRA_MAX_TIMERS - 1);
        return -EINVAL;
    }
    
    if(timer_callbacks[timer_id] == NULL) {
        printf("Warning: No timer callback registered for ID %d\n", timer_id);
        return -ENOENT;
    }
    
    timer_callbacks[timer_id] = NULL;
    
    printf("Unregistered timer callback for ID %d\n", timer_id);
    return 0; 
}

int akira_unregister_gpio_callback(int port, int pin) {
    initialize_callback();
    
    if(pin < 0 || pin >= AKIRA_MAX_GPIO_PINS_PER_PORT || 
       port < 0 || port >= AKIRA_MAX_GPIO_PORTS) {
        printf("Error: Invalid pin %d or port %d\n", pin, port);
        return -EINVAL;
    }
    
    for(int i = 0; i < AKIRA_MAX_CALLBACKS; i++) {
        if(gpio_callback_pins[i] == pin && gpio_callback_ports[i] == port) {
            if(gpio_callbacks[i] == NULL) {
                printf("Warning: GPIO slot found but callback is NULL for pin %d, port %d\n", 
                       pin, port);
                
                gpio_callback_pins[i] = -1;
                gpio_callback_ports[i] = -1;
                return -ENOENT;
            }
            
            gpio_callbacks[i] = NULL;
            gpio_callback_pins[i] = -1;
            gpio_callback_ports[i] = -1;
            
            printf("Unregistered GPIO callback for pin %d, port %d\n", pin, port);
            return 0;
        }
    }
    
    printf("Warning: No GPIO callback registered for pin %d, port %d\n", pin, port);
    return -ENOENT;
}

int akira_unregister_message_callback(const char *topic) {
    initialize_callback();
    
    if(!topic || topic[0] == '\0') {
        printf("Error: Topic is NULL or empty\n");
        return -EINVAL;
    }
    
    for(int i = 0; i < AKIRA_MAX_CALLBACKS; i++) {
        if(message_callback_topics[i][0] == '\0') {
            continue;
        }

        if(strcmp(message_callback_topics[i], topic) == 0) {
            if(message_callbacks[i] == NULL) {
                printf("Warning: Topic found but callback is NULL for topic '%s'\n", topic);
                message_callback_topics[i][0] = '\0';
                return -ENOENT;
            }
            
            message_callbacks[i] = NULL;
            message_callback_topics[i][0] = '\0';
            
            printf("Unregistered message callback for topic '%s'\n", topic);
            return 0;
        }
    }

    printf("Warning: No message callback registered for topic '%s'\n", topic);
    return -ENOENT; 
}

/**
 * Process pending events from the runtime.
 * Applications should call this function repeatedly in their main loop.
 * Each call processes up to a maximum number of events to prevent
 * starving the application code if events arrive very rapidly.
 */
void akira_process_events(void) {
    initialize_callback();

    int events_processed = 0;
    
    while (events_processed < MAX_EVENTS_PER_CALL) {
        akira_event_t event;
        
        int result = akira_get_event(&event);
        
        if (result) {
            break;
        }
        
        switch (event.type) {
            case AKIRA_EVENT_TYPE_TIMER: {
                uint32_t timer_id = event.data.timer.timer_id;
                dispatch_timer_event(timer_id);
                break;
            }
            
            case AKIRA_EVENT_TYPE_GPIO: {
                uint8_t pin = event.data.gpio.pin;
                uint8_t port = event.data.gpio.port;
                uint8_t state = event.data.gpio.state;
                
                dispatch_gpio_event(pin, port, state);
                break;
            }
            
            case AKIRA_EVENT_TYPE_MESSAGE: {
                const char *topic_ptr = (const char *)event.data.message.topic;
                const char *content_type_ptr = (const char *)event.data.message.content_type;
                const uint8_t *payload_ptr = (const uint8_t *)event.data.message.payload;
                uint32_t payload_len = event.data.message.payload_len;
                
                // Use static buffers to avoid excessive stack allocation
                static char topic_copy[AKIRA_MAX_TOPIC_LEN];
                static char content_type_copy[AKIRA_MAX_CONTENT_TYPE_LEN];
                static uint8_t payload_copy[AKIRA_MAX_PAYLOAD_LEN];
                
                if(topic_ptr == NULL) {
                    printf("Error: Message event with NULL topic pointer\n");
                    break;
                }
                strncpy(topic_copy, topic_ptr, AKIRA_MAX_TOPIC_LEN - 1);
                topic_copy[AKIRA_MAX_TOPIC_LEN - 1] = '\0';
                
                if(content_type_ptr != NULL) {
                    strncpy(content_type_copy, content_type_ptr, AKIRA_MAX_CONTENT_TYPE_LEN - 1);
                    content_type_copy[AKIRA_MAX_CONTENT_TYPE_LEN - 1] = '\0';
                } else {
                    content_type_copy[0] = '\0'; // Content type should be an optional field
                }
                
                uint32_t len_to_copy = (payload_len < AKIRA_MAX_PAYLOAD_LEN) ? 
                                    payload_len : AKIRA_MAX_PAYLOAD_LEN;
                
                if(payload_ptr != NULL && len_to_copy > 0) {
                    memcpy(payload_copy, payload_ptr, len_to_copy);
                } else {
                    len_to_copy = 0;
                }

                if(payload_len > AKIRA_MAX_PAYLOAD_LEN) {
                    printf("Warning: Payload truncated from %u to %u bytes\n", 
                        payload_len, AKIRA_MAX_PAYLOAD_LEN);
                }
                
                dispatch_message_event(topic_copy, content_type_copy, 
                                    payload_copy, len_to_copy);
                break;
            }
            
            default:
                printf("Unknown event type received: %d\n", event.type);
                break;
        }
        
        events_processed++;
    }
    
    if (events_processed == 0) {
        akira_system_sleep(IDLE_SLEEP_MS); // Sleep briefly if no events to avoid busy loop
    }
}
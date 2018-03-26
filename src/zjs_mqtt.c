// Copyright (c) 2018, Intel Corporation.

#ifdef BUILD_MODULE_MQTT

// C includes
#include <string.h>

// Zephyr includes
#include <net/mqtt.h>

// JerryScript includes
#include "jerryscript.h"

// ZJS includes
#include "zjs_common.h"
#include "zjs_event.h"
#include "zjs_net_config.h"
#include "zjs_util.h"

#ifdef CONFIG_NET_APP_SETTINGS
#ifdef CONFIG_NET_IPV6
#define ZEPHYR_ADDR     CONFIG_NET_APP_MY_IPV6_ADDR
#define SERVER_ADDR     CONFIG_NET_APP_PEER_IPV6_ADDR
#else
#define ZEPHYR_ADDR     CONFIG_NET_APP_MY_IPV4_ADDR
#define SERVER_ADDR	    CONFIG_NET_APP_PEER_IPV4_ADDR
#endif
#else
#ifdef CONFIG_NET_IPV6
#define ZEPHYR_ADDR     "2001:db8::1"
#define SERVER_ADDR     "2001:db8::2"
#else
#define ZEPHYR_ADDR     "192.168.1.1"
#define SERVER_ADDR     "192.168.1.2"
#endif
#endif

// Default MQTT configurations
#define SERVER_PORT                 1883
#define APP_CONNECT_TRIES           10
#define APP_SLEEP_MSECS             500
#define APP_TX_RX_TIMEOUT           300
#define APP_NET_INIT_TIMEOUT        10000
#define MQTT_URL_MAX                128

#define MQTT_CLIENT_ID     "zjs_mqtt_publisher"

static jerry_value_t zjs_mqtt_prototype;
static jerry_value_t zjs_mqtt_client_prototype;

static jerry_value_t mqtt_obj;

typedef struct client_handle {
    char *url;                            // url the client is connected to
    bool connected;                       // client is connected or not
    jerry_value_t client_obj;             // client object
	struct mqtt_connect_msg connect_msg;  // connect message structure
    struct mqtt_ctx mqtt_ctx;             // MQTT context
    struct client_handle *next;           // next client
} client_handle_t;


static client_handle_t *client_list = NULL;

static void zjs_mqtt_cleanup(void *native)
{
    jerry_release_value(zjs_mqtt_prototype);
    jerry_release_value(zjs_mqtt_client_prototype);
    jerry_release_value(mqtt_obj);
}

static const jerry_object_native_info_t mqtt_module_type_info = {
    .free_cb = zjs_mqtt_cleanup
};

static void zjs_mqtt_client_free_cb(void *native)
{
    client_handle_t *handle = (client_handle_t *)native;
    zjs_free(handle);
}

static const jerry_object_native_info_t client_type_info = {
    .free_cb = zjs_mqtt_client_free_cb
};

// Zephyr MQTT Client callbacks
static void mqtt_connect_cb(struct mqtt_ctx *mqtt_ctx)
{
    client_handle_t *handle = CONTAINER_OF(mqtt_ctx, client_handle_t, mqtt_ctx);

    ZJS_PRINT("Client connected\n");
    zjs_obj_add_readonly_boolean(handle->client_obj, "connected", true);
    zjs_obj_add_readonly_boolean(handle->client_obj, "reconnecting", false);
    zjs_defer_emit_event(handle->client_obj, "connect", NULL, 0, NULL, NULL);
}

static void mqtt_disconnect_cb(struct mqtt_ctx *mqtt_ctx)
{
    client_handle_t *handle = CONTAINER_OF(mqtt_ctx, client_handle_t, mqtt_ctx);

    ZJS_PRINT("Client connected\n");
    zjs_obj_add_readonly_boolean(handle->client_obj, "connected", false);
    zjs_obj_add_readonly_boolean(handle->client_obj, "reconnecting", false);
    zjs_defer_emit_event(handle->client_obj, "close", NULL, 0, NULL, NULL);
}

jerry_value_t create_packet_object(struct mqtt_publish_msg *msg,
                                   u16_t pkt_id,
                                   enum mqtt_packet type)
{
    ZVAL packet = jerry_create_object();
    switch (type) {
    case MQTT_PUBLISH:
        zjs_set_property(packet, "cmd", jerry_create_string("publish"));
        zjs_set_property(packet, "messageId", jerry_create_number(pkt_id));
        switch (msg->qos)
        {
            case MQTT_QoS0:
                zjs_set_property(packet, "qos", jerry_create_number(0));
                break;
            case MQTT_QoS1:
                zjs_set_property(packet, "qos", jerry_create_number(1));
                break;
            case MQTT_QoS2:
                zjs_set_property(packet, "qos", jerry_create_number(2));
                break;
            default:
                // default QoS level is 2
                zjs_set_property(packet, "qos", jerry_create_number(2));
        }
        zjs_set_property(packet, "dup", jerry_create_boolean((bool)msg->dup));
        zjs_set_property(packet, "topic", jerry_create_string(msg->topic));
        zjs_set_property(packet, "paylod", jerry_create_string(msg->msg));
        zjs_set_property(packet, "retain", jerry_create_boolean((bool)msg->retain));
        break;
    case MQTT_PUBREL:
        return jerry_create_null();
    default:
        return jerry_create_null();
    }

    return packet;
}

static int mqtt_publish_tx_cb(struct mqtt_ctx *ctx,
                              u16_t pkt_id,
                              enum mqtt_packet type)
{
    int rc = 0;
    return rc;
}

static int mqtt_publish_rx_cb(struct mqtt_ctx *mqtt_ctx,
                              struct mqtt_publish_msg
                              *msg, u16_t pkt_id, enum mqtt_packet type)
{
    int rc = 0;
    char* str = NULL;
    client_handle_t *handle = CONTAINER_OF(mqtt_ctx, client_handle_t, mqtt_ctx);

    ZJS_PRINT("Publish received\n");
    switch (type) {
    case MQTT_PUBLISH:
        str = "MQTT_PUBLISH";
        ZJS_PRINT("Publish [%s:%d] <%s> msg: %s\n", __func__, __LINE__,
                  str, msg->msg);

        ZVAL topic = jerry_create_string(msg->topic);
        ZVAL message = jerry_create_string(msg->msg);
        ZVAL packet = create_packet_object(msg, pkt_id, type);
        jerry_value_t argv[3] = { topic, message, packet };
        zjs_emit_event(handle->client_obj, "message", argv, 3);
        rc = 0;
        break;
    case MQTT_PUBREL:
        str = "MQTT_PUBREL";
        ZJS_PRINT("PUBREL [%s:%d] <%s> packet id: %u\n", __func__, __LINE__,
                  str, pkt_id);
        rc = 0;
    default:
        rc = -EINVAL;
        str = "Invalid MQTT packet";
    }

    return rc;
}

static int mqtt_subscriber_cb(struct mqtt_ctx *mqtt_ctx,
                              u16_t pkt_id,
                              u8_t items, enum mqtt_qos qos[])
{
    int rc = 0;
    //client_handle_t *handle = CONTAINER_OF(mqtt_ctx, client_handle_t, mqtt_ctx);
    ZJS_PRINT("subscribe [%s:%d] items: %d packet id: %u", __func__, __LINE__,
              items, pkt_id);

    return rc;
}

static int mqtt_unsubscribe_cb(struct mqtt_ctx *mqtt_ctx, u16_t pkt_id)
{
    int rc = 0;
    //client_handle_t *handle = CONTAINER_OF(mqtt_ctx, client_handle_t, mqtt_ctx);
    ZJS_PRINT("unsubscribe [%s:%d] packet id: %u", __func__, __LINE__, pkt_id);

    return rc;
}

static void mqtt_malformed_cb(struct mqtt_ctx *mqtt_ctx, u16_t pkt_type)
{
    //client_handle_t *handle = CONTAINER_OF(mqtt_ctx, client_handle_t, mqtt_ctx);
    ZJS_PRINT("malformed [%s:%d] pkt_type: %u\n", __func__, __LINE__, pkt_type);
}

static client_handle_t* create_mqtt_client(char *url,
                                           int port)
{
    client_handle_t *client_handle = zjs_malloc(sizeof(client_handle_t));
    if (!client_handle) {
        ERR_PRINT("failed to allocate client handle\n");
        return NULL;
    }
    memset(client_handle, 0, sizeof(client_handle_t));

    u32_t len = strlen(url);
    client_handle->url = zjs_malloc(len);
    if (!client_handle->url) {
        ERR_PRINT("failed to allocate url\n");
        zjs_free(client_handle);
        return NULL;
    }

    jerry_value_t client_obj = zjs_create_object();
    strcpy(client_handle->url, url);

    /* connect, disconnect and malformed may be set to NULL */
    client_handle->mqtt_ctx.connect = mqtt_connect_cb;
    client_handle->mqtt_ctx.disconnect = mqtt_disconnect_cb;
    client_handle->mqtt_ctx.malformed = mqtt_malformed_cb;
    client_handle->mqtt_ctx.publish_tx = mqtt_publish_tx_cb;
    client_handle->mqtt_ctx.publish_rx = mqtt_publish_rx_cb;
    client_handle->mqtt_ctx.subscribe = mqtt_subscriber_cb;
    client_handle->mqtt_ctx.unsubscribe = mqtt_unsubscribe_cb;
    client_handle->mqtt_ctx.net_init_timeout = APP_NET_INIT_TIMEOUT;
    client_handle->mqtt_ctx.net_timeout = APP_TX_RX_TIMEOUT;
    client_handle->mqtt_ctx.peer_addr_str = url;
    client_handle->mqtt_ctx.peer_port = port;

    int rc = mqtt_init(&client_handle->mqtt_ctx, MQTT_APP_SUBSCRIBER);
    if (rc != 0) {
        ERR_PRINT("failed to init mqtt client\n");
        zjs_free(client_handle->url);
        zjs_free(client_handle);
        return NULL;
    }

    zjs_obj_add_readonly_boolean(client_obj, "connected", false);
    zjs_obj_add_readonly_boolean(client_obj, "reconnecting", false);
    client_handle->client_obj = jerry_acquire_value(client_obj);
    zjs_make_emitter(client_obj, zjs_mqtt_client_prototype, client_handle, NULL);
    jerry_set_object_native_pointer(client_obj, client_handle, &client_type_info);

    return client_handle;
}

// In this routine we block until the connected variable is 1
static int try_to_connect(client_handle_t *client_handle)
{
    int i = 0;

    while (i++ < APP_CONNECT_TRIES && !client_handle->mqtt_ctx.connected) {
        int rc;
        rc = mqtt_tx_connect(&client_handle->mqtt_ctx, &client_handle->connect_msg);
        k_sleep(APP_SLEEP_MSECS);
        DBG_PRINT("mqtt_tx_connect %d\n", rc);
        if (rc != 0) {
            continue;
        }
    }

    if (client_handle->mqtt_ctx.connected) {
        return 0;
    }

    return -EINVAL;
}

static ZJS_DECL_FUNC(zjs_mqtt_connect)
{
    ZJS_VALIDATE_ARGS(Z_STRING, Z_OPTIONAL Z_OBJECT);

    char *url = NULL;
    int port = 0;
    jerry_size_t str_size = MQTT_URL_MAX;

    url = zjs_alloc_from_jstring(argv[0], &str_size);
    if (!url) {
        return zjs_error("url is too long\n");
    }

    client_handle_t *client_handle = NULL;
    client_handle = create_mqtt_client(url, port);
    if (!client_handle) {
        zjs_free(url);
        return zjs_error("Failed to create MQTT client");
    }

    // add new client to client list
    ZJS_LIST_PREPEND(client_handle_t, client_list, client_handle);
    DBG_PRINT("client created, url=%s\n", client_handle->url);

    zjs_free(url);

    /* The connect message will be sent to the MQTT server (broker).
     * If clean_session here is 0, the mqtt_ctx clean_session variable
     * will be set to 0 also. Please don't do that, set always to 1.
     * Clean session = 0 is not yet supported.
     */
    client_handle->connect_msg.client_id = MQTT_CLIENT_ID;
    client_handle->connect_msg.client_id_len = strlen(MQTT_CLIENT_ID);
    client_handle->connect_msg.clean_session = 1;

    // client_handle->connect_data = "CONNECTED";
    // client_handle->disconnect_data = "DISCONNECTED";
    // client_handle->publish_data = "PUBLISH";

    /* we will always connect as publisher rather than subscriber,
     * since we don't know until runtime if the client will be publishing
     * messages, so at least we can have both TX/RX callbacks set up
     */
    mqtt_init(&client_handle->mqtt_ctx, MQTT_APP_PUBLISHER);

    int rc = try_to_connect(client_handle);
    if (rc != 0) {
        return ZJS_ERROR("MQTT failed to connect\n");
    }

    DBG_PRINT("MQTT connected\n");
    return client_handle->client_obj;
}

static ZJS_DECL_FUNC(client_publish)
{
    ZJS_VALIDATE_ARGS(Z_STRING, Z_STRING Z_BUFFER, Z_OPTIONAL Z_OBJECT,
                      Z_OPTIONAL Z_FUNCTION);

    return ZJS_UNDEFINED;
}

static ZJS_DECL_FUNC(client_subscribe)
{
    ZJS_VALIDATE_ARGS(Z_STRING, Z_OPTIONAL Z_OBJECT, Z_OPTIONAL Z_FUNCTION);

    return ZJS_UNDEFINED;
}

static ZJS_DECL_FUNC(client_unsubscribe)
{
    ZJS_VALIDATE_ARGS(Z_STRING, Z_OPTIONAL Z_FUNCTION);

    return ZJS_UNDEFINED;
}

static ZJS_DECL_FUNC(client_end)
{
    ZJS_VALIDATE_ARGS(Z_OPTIONAL Z_BOOL, Z_OPTIONAL Z_FUNCTION);

    ZJS_GET_HANDLE(this, client_handle_t, handle, client_type_info);

    mqtt_close(&handle->mqtt_ctx);

    return ZJS_UNDEFINED;
}

static ZJS_DECL_FUNC(client_reconnect)
{
    return ZJS_UNDEFINED;
}

static jerry_value_t zjs_mqtt_init()
{
    zjs_native_func_t mqtt_array[] = {
            { zjs_mqtt_connect, "connect" },
            { NULL, NULL }
    };
    zjs_native_func_t client_array[] = {
            { client_publish, "publish" },
            { client_subscribe, "subscribe" },
            { client_unsubscribe, "unsubscribe" },
            { client_end, "end" },
            { client_reconnect, "reconnect" },
            { NULL, NULL }
    };

    // MQTT object prototype
    zjs_mqtt_prototype = zjs_create_object();
    zjs_obj_add_functions(zjs_mqtt_prototype, mqtt_array);

    // Client object prototype
    zjs_mqtt_client_prototype = zjs_create_object();
    zjs_obj_add_functions(zjs_mqtt_client_prototype, client_array);

    mqtt_obj = zjs_create_object();
    jerry_set_prototype(mqtt_obj, zjs_mqtt_prototype);

    // Set up cleanup function for when the object gets freed
    jerry_set_object_native_pointer(mqtt_obj, NULL, &mqtt_module_type_info);
    return mqtt_obj;
}

JERRYX_NATIVE_MODULE(mqtt, zjs_mqtt_init)
#endif  // BUILD_MODULE_MQTT

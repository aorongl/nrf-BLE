/**
 * @author 敖荣良
 * @date 2026.11.4
 * @brief
 * 功能：蓝牙GATT服务，实现自广播，收发数据(默认十六进制)，第一次绑定后实现主机可发起重连
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>

#define DEVICE_NAME		CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN		(sizeof(DEVICE_NAME) - 1)

/**
 * @brief 
 * 分别为服务、TX(主机给从机发信号)、RX(从机给主机发信号)的UUID
 */
#define SRV_UUID BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0Xe1d71f39, 0X3282, 0X47c6, 0X88bc, 0X599efe370503))
#define TX_UUID  BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0X345ffffa, 0Xd20b, 0X4e45, 0X976e, 0X907fdf329c40))
#define RX_UUID  BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0Xca06e04a, 0Xa3b8, 0X4b23, 0Xb658, 0X5a7120e2a09f))

static struct bt_conn *active_conn = NULL;
static uint16_t notify_ccc_val;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),//标志位：低功耗|可发现广播
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_128_ENCODE(0Xe1d71f39, 0X3282, 0X47c6, 0X88bc, 0X599efe370503)),
};

static void notif_enabled(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_ccc_val = value;

	printk("%s() - %s\n", __func__, (value ? "Enabled" : "Disabled"));
}

static ssize_t received(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len,
				uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(offset);
	ARG_UNUSED(flags);

	printk("%s() - Len: %d, buf: %p\n", __func__, len, buf);
	if (!buf || len == 0) {
        printk("received: empty write, ignoring\n");
        return len;
    }
    /* 正常打印数据 */
    printk("data: %.*s\n", len, (const char *)buf);
	return len;
}

/**
 * @brief
 * 定义GATT服务
 */
BT_GATT_SERVICE_DEFINE(gatt_svc, 
	
	BT_GATT_PRIMARY_SERVICE(SRV_UUID),

	/* 可写特征：手机发数据给设备 */
	BT_GATT_CHARACTERISTIC(RX_UUID,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE_ENCRYPT,
			       NULL, received, NULL),

	/* Notify特征：设备发数据给手机，带CCCD描述符 */
	BT_GATT_CHARACTERISTIC(TX_UUID,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       NULL, NULL, NULL),
	BT_GATT_CCC(notif_enabled, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);


/** 
 * @brief
 * 连接回调，保存当前连接 
 */
static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("connect fail err %d\n", err);
		active_conn = NULL;
		return;
	}
	
	printk("Connected!\n");
	active_conn = bt_conn_ref(conn);

	int ret = bt_conn_set_security(conn, BT_SECURITY_L2);
	if(ret !=0){
		printk("req security ret:%d\n",ret);
	}
}

/** 
 * @brief
 * 断开连接回调
 */
static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason: %d\n", reason);
	if (active_conn) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}
	notify_ccc_val = 0U;
}


/**
 * @brief 
 * 发送自定义Notify数据
 * @param data
 * @param len
 */
static int my_gatt_notify_send(const void *data, uint16_t len)
{
	if (!active_conn || !(notify_ccc_val & BT_GATT_CCC_NOTIFY)) {
		return -ENOTCONN;
	}

	/* 获取notify特征的属性句柄 */
	const struct bt_gatt_attr *notify_attr = bt_gatt_find_by_uuid(
		gatt_svc.attrs, gatt_svc.attr_count, TX_UUID);
	if (!notify_attr) {
		return -ENOENT;
	}

	return bt_gatt_notify(active_conn, notify_attr, data, len);
}


/**
 * @brief
 * 工作项函数
 * @param
 * work
 */
static void BLE_Work_Handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Failed to start advertising: %d\n", err);
		return;
	}

	printk("Initialization complete\n");

}

/**
 * @brief 注册系统工作队列项
 */
K_WORK_DEFINE(BLE_work, BLE_Work_Handler);
void adv_start()
{
	k_work_submit(&BLE_work);
	printk("Submission complete\n");
}

/**
 * @callergraph
 * 
 * @brief
 * 连接回调结构体 
 */
static struct bt_conn_cb conn_callbacks = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.recycled = adv_start,
};

/**
 * @brief
 * 注册 auth 认证回调
 * @details
 */
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.passkey_display = auth_passkey_display,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
};


/**
 * 
 */
int main(void)
{
	int err;
	// bt_addr_le_t addr;

	printk("Sample - Bluetooth Peripheral\n");

	/* 注册连接事件回调 */
	err = bt_conn_cb_register(&conn_callbacks);
	if (err) {
		printk("Failed to register callback: %d\n", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Failed to enable bluetooth: %d\n", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_conn_auth_cb_register(&auth_cb_display);
	if (err) {
		printk("auth cb register err %d\n", err);
	}

	adv_start();

	while (true) {
		const char *hello_world = "Hello World!\n";

		k_sleep(K_SECONDS(3));

		err = my_gatt_notify_send(hello_world, strlen(hello_world));
		printk("Data send - Result: %d\n", err);

		if (err < 0 && (err != -EAGAIN) && (err != -ENOTCONN)) {
			return err;
		}
	}

	return 0;
}


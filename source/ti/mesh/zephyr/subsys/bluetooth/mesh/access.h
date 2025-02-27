/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Tree walk return codes */
enum bt_mesh_walk {
	BT_MESH_WALK_STOP,
	BT_MESH_WALK_CONTINUE,
};

void bt_mesh_elem_register(struct bt_mesh_elem *elem, uint8_t count);

uint8_t bt_mesh_elem_count(void);

/* Find local element based on unicast or group address */
struct bt_mesh_elem *bt_mesh_elem_find(uint16_t addr);

bool bt_mesh_model_has_key(struct bt_mesh_model *mod, uint16_t key);

struct bt_mesh_model *bt_mesh_model_root(struct bt_mesh_model *mod);
void bt_mesh_model_tree_walk(struct bt_mesh_model *root,
			     enum bt_mesh_walk (*cb)(struct bt_mesh_model *mod,
						     uint32_t depth,
						     void *user_data),
			     void *user_data);

uint16_t *bt_mesh_model_find_group(struct bt_mesh_model **mod, uint16_t addr);

void bt_mesh_model_foreach(void (*func)(struct bt_mesh_model *mod,
					struct bt_mesh_elem *elem,
					bool vnd, bool primary,
					void *user_data),
			   void *user_data);

int32_t bt_mesh_model_pub_period_get(struct bt_mesh_model *mod);

void bt_mesh_comp_provision(uint16_t addr);
void bt_mesh_comp_unprovision(void);

uint16_t bt_mesh_primary_addr(void);

const struct bt_mesh_comp *bt_mesh_comp_get(void);

struct bt_mesh_model *bt_mesh_model_get(bool vnd, uint8_t elem_idx, uint8_t mod_idx);

void bt_mesh_model_recv(struct bt_mesh_net_rx *rx, struct net_buf_simple *buf);

int bt_mesh_comp_register(const struct bt_mesh_comp *comp);

void bt_mesh_model_pending_store(void);
void bt_mesh_model_bind_store(struct bt_mesh_model *mod);
void bt_mesh_model_sub_store(struct bt_mesh_model *mod);
void bt_mesh_model_pub_store(struct bt_mesh_model *mod);
void bt_mesh_model_settings_commit(void);

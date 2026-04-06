/*
 * IoT-Bridge calls ip_napt_table_clear() on link down; ESP-IDF 5.2+ lwIP may not
 * export it when ip4_napt.patch is skipped (idf_version >= 5.2.0 in patches.list).
 * Weak so a future IDF/lwIP strong definition overrides this stub.
 */
void __attribute__((weak)) ip_napt_table_clear(void) {}

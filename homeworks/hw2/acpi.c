#include "acpi.h"
#include "common.h"
#include "panic.h"
#include "multiboot.h"

struct acpi_sdt* acpi_lookup_sdt(struct acpi_sdt* root, const char* signature) {
    size_t sz = (root->header.length - sizeof(root->header)) / 4;
    for (size_t i = 0; i < sz; i++) {
        struct acpi_sdt* sdt = (void*)(uint64_t)root->entries[i];
        if (memcmp(signature, &sdt->header.signature, 4) == 0) {
            return sdt;
        }
    }
    return NULL;
}

static struct acpi_sdt* rsdt = NULL;

struct acpi_sdt* acpi_lookup_rsdt(const char* signature) {
    return acpi_lookup_sdt(rsdt, signature);
}

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_addr;
} __attribute__ ((packed));


static bool validate_checksum(void *data, size_t size) {
    uint32_t sum = 0;

    for (size_t i = 0; i < size; ++i) {
        sum += ((uint8_t *)data)[i];
    }

    return (sum & 0xff) == 0;
}

void acpi_init() {
    struct multiboot_tag* mb_tag = multiboot_lookup_tag(MULTIBOOT_TAG_ACPI_OLD_RSDP);
    if (mb_tag == NULL) {
        panic("no multiboot tag with ACPI RSDP addres");
    }
    struct acpi_rsdp* rsdp = (void*)(uint64_t)&mb_tag->data;

    // As per ACPI standard, 5.2.5.3
    BUG_ON(!validate_checksum(rsdp, 20));

    rsdt = (void*)(uint64_t)rsdp->rsdt_addr;
    printk("found RSDT at %p\n", rsdt);

    BUG_ON(!validate_checksum(rsdt, rsdt->header.length));

    const unsigned cnt = (rsdt->header.length - sizeof(rsdt->header)) / sizeof(uint32_t);

    for (unsigned i = 0; i < cnt; ++i) {
        struct acpi_sdt* sdt = (void*)(uint64_t)rsdt->entries[i];
        
        if (!validate_checksum(sdt, sdt->header.length)) {
            char signature[5] = "";
            *(uint32_t *)signature = *(uint32_t *)sdt->header.signature;
            signature[4] = 0;

            panic("Wrong checksum for header #%u at %p (\"%s\")", i, sdt, signature);
        }
    }

    #if 0
    const void *end = ((uint8_t *)rsdt) + rsdt->header.length;
    for (struct acpi_sdt_header *cur = (void *)rsdt->entries;
         (void *)cur < end;
         cur = ((void *)cur) + cur->length) {
        BUG_ON(!validate_checksum(cur, cur->length));
    }
    #endif
}

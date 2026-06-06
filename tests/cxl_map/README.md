# AEP CXL Map Test

This test exercises the first-stage AEP CXL mapping syscall. It creates an AEP
template, maps a page-aligned physical range into the template devmap region,
and optionally joins the template.

Build:

```sh
make
```

Run:

```sh
sudo ./cxl_map --phys=0xADDR --size=0xSIZE [--exec] [--join] [--no-exit]
```

Before running `cxl_map` in the QEMU guest, prepare the emulated CXL device:

```sh
cd ../
sudo ./setup_cxl_dax.sh
```

Arguments:

- `--phys=0xADDR`: page-aligned CXL physical base address.
- `--size=0xSIZE`: page-aligned mapping size.
- `--exec`: request executable AEP private mapping protection.
- `--join`: call `aep_join(template_id)` after mapping.
- `--rw-test`: join the template, write two patterns through the mapped AEP
  address, and verify that reads return the expected values.
- `--no-exit`: keep the template allocated when the process exits.

The test requires an AEP-enabled kernel with syscall `aep_map_cxl` available.

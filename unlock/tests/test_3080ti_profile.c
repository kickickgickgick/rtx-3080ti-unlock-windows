#include "../vendor/vbios_610.h"
#include "../vendor/prep_610.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message)
{
	fprintf(stderr, "FAIL: %s\n", message);
	return 1;
}

int main(int argc, char **argv)
{
	struct gpu_unlock_fwsec fw;
	u8 *rom = NULL, *image = NULL;
	u32 signature_offset = 0;
	FILE *file;
	long bytes;
	int result = 1;

	if (argc != 2)
		return fail("usage: test-3080ti-profile PROM.BIN");
	file = fopen(argv[1], "rb");
	if (!file)
		return fail("cannot open RTX 3080 Ti PROM fixture");
	if (fseek(file, 0, SEEK_END) || (bytes = ftell(file)) != 1024 * 1024L ||
	    fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return fail("PROM fixture must be exactly 1 MiB");
	}
	rom = malloc((size_t)bytes);
	if (!rom || fread(rom, 1, (size_t)bytes, file) != (size_t)bytes) {
		fclose(file);
		free(rom);
		return fail("cannot read complete PROM fixture");
	}
	fclose(file);

	memset(&fw, 0, sizeof(fw));
	if (gpu_unlock_vbios_fwsec(rom, (size_t)bytes, &fw))
		goto out_parser;
	if (fw.engine_mask != 0x400 || fw.ucode_id != 9 || fw.sig_count != 3 ||
	    fw.sig_versions != 0x7 || !fw.image_size ||
	    (u64)fw.image_off + fw.image_size > (u64)bytes ||
	    (u64)fw.sig_off + fw.sig_bytes > (u64)bytes)
		goto out_shape;
	if (gpu_unlock_fuse_version(3) != 2 ||
	    gpu_unlock_fwsec_sig_offset(fw.sig_versions,
	      gpu_unlock_fuse_version(3), 384, &signature_offset) ||
	    signature_offset != 768 || signature_offset + 384 > fw.sig_bytes)
		goto out_signature;

	image = malloc(fw.image_size);
	if (!image)
		goto out_memory;
	memcpy(image, rom + fw.image_off, fw.image_size);
	if (gpu_unlock_fwsec_patch_frts(image, &fw, 0x2ffe00000ULL))
		goto out_patch;

	printf("RTX 3080 Ti PROM FWSEC profile: PASS "
	       "(engine=%#x ucode=%u signatures=%u versions=%#x selected=%u)\n",
	       fw.engine_mask, fw.ucode_id, fw.sig_count, fw.sig_versions,
	       gpu_unlock_fuse_version(3));
	result = 0;
	goto out;

out_patch:
	fputs("FAIL: RTX 3080 Ti FWSEC FRTS patch rejected\n", stderr);
	goto out;
out_memory:
	fputs("FAIL: image allocation failed\n", stderr);
	goto out;
out_signature:
	fputs("FAIL: fuse sample 3 did not select signature version 2\n", stderr);
	goto out;
out_shape:
	fputs("FAIL: parsed RTX 3080 Ti FWSEC descriptor differs from captured profile\n", stderr);
	goto out;
out_parser:
	fputs("FAIL: RTX 3080 Ti PROM was rejected by FWSEC parser\n", stderr);
out:
	free(image);
	free(rom);
	return result;
}

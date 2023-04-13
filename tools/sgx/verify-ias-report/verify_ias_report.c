/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2020 Invisible Things Lab
 *                    Rafal Wojdyla <omeg@invisiblethingslab.com>
 */

#define _GNU_SOURCE

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "ias.h"
#include "quote.h"
#include "util.h"

struct option g_options[] = {
    { "help", no_argument, 0, 'h' },
    { "verbose", no_argument, 0, 'v' },
    { "msb", no_argument, 0, 'm' },
    { "report-path", required_argument, 0, 'r' },
    { "sig-path", required_argument, 0, 's' },
    { "nonce", required_argument, 0, 'n' },
    { "mr-signer", required_argument, 0, 'S' },
    { "mr-enclave", required_argument, 0, 'E' },
    { "report-data", required_argument, 0, 'R' },
    { "isv-prod-id", required_argument, 0, 'P' },
    { "isv-svn", required_argument, 0, 'S' },
    { "ias-pubkey", required_argument, 0, 'i' },
    /* below 4 long options don't have corresponding short names (see 3rd arg in `getopt_long()`),
     * but they return '1', '2', ... chars to use in the switch statement */
    { "allow-debug-enclave", no_argument, 0, '1' },
    { "allow-outdated-tcb", no_argument, 0, '2' },
    { "allow-hw-config-needed", no_argument, 0, '3' },
    { "allow-sw-hardening-needed", no_argument, 0, '4' },
    { 0, 0, 0, 0 }
};

static void usage(const char* exec) {
    INFO("Usage: %s [options]\n", exec);
    INFO("Available options:\n");
    INFO("  --help, -h                   Display this help\n");
    INFO("  --verbose, -v                Enable verbose output\n");
    INFO("  --msb, -m                    Print/parse hex strings in big-endian order\n");
    INFO("  --report-path, -r PATH       Path to the IAS report\n");
    INFO("  --sig-path, -s PATH          Path to the IAS report's signature\n");
    INFO("  --nonce, -n STRING           Nonce that's expected in the report (optional)\n");
    INFO("  --mr-signer, -S STRING       Expected mr_signer field (hex string, optional)\n");
    INFO("  --mr-enclave, -E STRING      Expected mr_enclave field (hex string, optional)\n");
    INFO("  --report-data, -R STRING     Expected report_data field (hex string, optional)\n");
    INFO("  --isv-prod-id, -P NUMBER     Expected isv_prod_id field (uint16_t, optional)\n");
    INFO("  --isv-svn, -V NUMBER         Expected isv_svn field (uint16_t, optional)\n");
    INFO("  --ias-pubkey, -i PATH        Path to IAS public RSA key (PEM format, optional)\n");
    INFO("  --allow-debug-enclave        Allow debug enclave (SGXREPORT.ATTRIBUTES.DEBUG = 1)\n");
    INFO("  --allow-outdated-tcb         Treat IAS status code GROUP_OUT_OF_DATE as OK\n");
    INFO("  --allow-hw-config-needed     Treat IAS status code CONFIGURATION_NEEDED as OK\n");
    INFO("  --allow-sw-hardening-needed  Treat IAS status code SW_HARDENING_NEEDED as OK\n");
    INFO("\n");
    INFO("To treat the IAS status code CONFIGURATION_AND_SW_HARDENING_NEEDED as OK, set both\n");
    INFO("--allow-hw-config-needed and --allow-sw-hardening-needed options.\n");
}

int main(int argc, char* argv[]) {
    int ret;

    int option            = 0;
    char* report_path     = NULL;
    size_t report_size    = 0;
    char* sig_path        = NULL;
    size_t sig_size       = 0;
    char* nonce           = NULL;
    char* mrsigner        = NULL;
    char* mrenclave       = NULL;
    char* report_data     = NULL;
    char* isv_prod_id     = NULL;
    char* isv_svn         = NULL;
    char* ias_pubkey_path = NULL;

    bool allow_debug_enclave       = false;
    bool allow_outdated_tcb        = false;
    bool allow_hw_config_needed    = false;
    bool allow_sw_hardening_needed = false;

    endianness_t endian = ENDIAN_LSB;

    // parse command line
    while (true) {
        option = getopt_long(argc, argv, "hvmr:s:n:S:E:R:P:V:i:", g_options, NULL);
        if (option == -1)
            break;

        switch (option) {
            case 'h':
                usage(argv[0]);
                return 0;
            case 'v':
                set_verbose(true);
                break;
            case 'm':
                endian = ENDIAN_MSB;
                break;
            case 'r':
                report_path = optarg;
                break;
            case 's':
                sig_path = optarg;
                break;
            case 'n':
                nonce = optarg;
                break;
            case 'S':
                mrsigner = optarg;
                break;
            case 'E':
                mrenclave = optarg;
                break;
            case 'R':
                report_data = optarg;
                break;
            case 'P':
                isv_prod_id = optarg;
                break;
            case 'V':
                isv_svn = optarg;
                break;
            case 'i':
                ias_pubkey_path = optarg;
                break;
            case '1':
                allow_debug_enclave = true;
                break;
            case '2':
                allow_outdated_tcb = true;
                break;
            case '3':
                allow_hw_config_needed = true;
                break;
            case '4':
                allow_sw_hardening_needed = true;
                break;
            default:
                usage(argv[0]);
                return -1;
        }
    }

    set_endianness(endian);

    if (!report_path || !sig_path) {
        usage(argv[0]);
        return -1;
    }

    void* report = read_file(report_path, &report_size, /*buffer=*/NULL);
    if (!report) {
        ERROR("Failed to read report file '%s'\n", report_path);
        return -1;
    }

    void* sig = read_file(sig_path, &sig_size, /*buffer=*/NULL);
    if (!sig) {
        ERROR("Failed to read report signature file '%s'\n", sig_path);
        return -1;
    }

    char* ias_pubkey = NULL;
    size_t ias_pubkey_size = 0;

    if (ias_pubkey_path) {
        void* buf = read_file(ias_pubkey_path, &ias_pubkey_size, /*buffer=*/NULL);
        if (!buf) {
            ERROR("Failed to read IAS pubkey file '%s'\n", ias_pubkey_path);
            return -1;
        }

        // Need to add NULL terminator
        ias_pubkey = calloc(1, ias_pubkey_size + 1);
        if (!ias_pubkey) {
            ERROR("No memory\n");
            return -1;
        }

        memcpy(ias_pubkey, buf, ias_pubkey_size);
        free(buf);
        DBG("Using IAS public key from file '%s'\n", ias_pubkey_path);
    }

    uint8_t* report_quote_body = NULL;
    size_t quote_body_size = 0;
    char enclave_quote_status[128] = {0};

    /* IAS returns a truncated SGX quote without signature fields (only the SGX quote body) */
    ret = ias_verify_report_extract_quote(report, report_size, sig, sig_size,
                                          allow_outdated_tcb, allow_hw_config_needed,
                                          allow_sw_hardening_needed, nonce, ias_pubkey,
                                          &enclave_quote_status, &report_quote_body,
                                          &quote_body_size);
    if (ret < 0) {
        if (get_verbose()) {
            ERROR("Failed to verify IAS attestation report or extract the SGX quote from it, "
                  "enclave quote status: %s\n", enclave_quote_status);
        }
        return ret;
    }

    /* verify that obtained SGX quote (extracted from IAS report) has reasonable size */
    if (quote_body_size < sizeof(sgx_quote_body_t) || quote_body_size > SGX_QUOTE_MAX_SIZE) {
        ERROR("SGX quote returned in IAS report has unexpected size %lu\n", quote_body_size);
        free(report_quote_body);
        return -1;
    }

    ret = verify_quote_body((sgx_quote_body_t*)report_quote_body, mrsigner, mrenclave, isv_prod_id,
                            isv_svn, report_data, /*expected_as_str=*/true);
    if (ret < 0) {
        free(report_quote_body);
        return ret;
    }

    ret = verify_quote_body_enclave_attributes((sgx_quote_body_t*)report_quote_body,
                                               allow_debug_enclave);
    free(report_quote_body);
    return ret;
}

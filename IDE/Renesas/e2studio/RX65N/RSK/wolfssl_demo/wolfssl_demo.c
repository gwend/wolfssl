/* wolfssl_demo.c
 *
 * Copyright (C) 2006-2021 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "platform/iot_network.h"
#include "platform.h"


#include <wolfssl/wolfcrypt/settings.h>
#include "wolfssl/ssl.h"
#include <wolfssl/wolfio.h>
#include "wolfssl/certs_test.h"
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl_demo.h"

#if defined(BENCHMARK)
    #include "r_cmt_rx_if.h"
#endif

#if defined(WOLFSSL_RENESAS_TSIP_TLS)
    #include "key_data.h"
    #include <wolfssl/wolfcrypt/port/Renesas/renesas-tsip-crypt.h>

    extern const st_key_block_data_t g_key_block_data;
    user_PKCbInfo            guser_PKCbInfo;
#endif



#define YEAR 2022
#define MON  1
#define FREQ 10000 /* Hz */

#define TLSSERVER_IP      "192.168.1.14"
#define TLSSERVER_PORT    11111

typedef struct func_args {
    int    argc;
    char** argv;
    int    return_code;
} func_args;

static long         tick;
static int          tmTick;
static WOLFSSL_CTX* client_ctx;

#if defined(WOLFSSL_RENESAS_TSIP_TLS)
uint32_t g_encrypted_root_public_key[140];
static   TsipUserCtx userContext;
#endif

/* time
 * returns seconds from EPOCH
 */
time_t time(time_t *t)
{
    (void)t;
    return ((YEAR-1970)*365+30*MON)*24*60*60 + tmTick++;
}

/* timeTick
 * called periodically by H/W timer to increase tmTick.
 */
static void timeTick(void* pdata)
{
    (void)pdata;
    tick++;
}

double current_time(int reset)
{
      if(reset) tick = 0 ;
      return ((double)tick/FREQ) ;
}

void wolfcrypt_test();
void benchmark_test();



/* --------------------------------------------------------*/
/*  Benchmark_demo                                         */
/* --------------------------------------------------------*/
static void Benchmark_demo(void)
{
    uint32_t channel;
    R_CMT_CreatePeriodic(FREQ, &timeTick, &channel);

    printf("Start wolfCrypt Benchmark\n");
    benchmark_test();
    printf("End wolfCrypt Benchmark\n");
}

/* --------------------------------------------------------*/
/*  CryptTest_demo                                         */
/* --------------------------------------------------------*/
static void CryptTest_demo(void)
{
    func_args args = { 0 };
    int ret;

    if ((ret = wolfCrypt_Init()) != 0) {
         printf("wolfCrypt_Init failed %d\n", ret);
    }

    printf("Start wolfCrypt Test\n");
    wolfcrypt_test(args);
    printf("End wolfCrypt Test\n");

    if ((ret = wolfCrypt_Cleanup()) != 0) {
        printf("wolfCrypt_Cleanup failed %d\n", ret);
    }
}

/* --------------------------------------------------------*/
/*  Tls_client_demo                                        */
/* --------------------------------------------------------*/
static void Tls_client_init(const char* cipherlist)
{

    #ifndef NO_FILESYSTEM
        #ifdef USE_ECC_CERT
            char *cert       = "./certs/ca-ecc-cert.pem";
        #else
            char *cert       = "./certs/ca-cert.pem";
        #endif
    #else
        #if defined(USE_ECC_CERT) && defined(USE_CERT_BUFFERS_256) 
            const unsigned char *cert       = ca_ecc_cert_der_256;
            #define  SIZEOF_CERT sizeof_ca_ecc_cert_der_256
        #else
            const unsigned char *cert       = ca_cert_der_2048;
            #define  SIZEOF_CERT sizeof_ca_cert_der_2048
        #endif
    #endif


    client_ctx = NULL;

    wolfSSL_Init();

    #ifdef DEBUG_WOLFSSL
        wolfSSL_Debugging_ON();
    #endif

    /* Create and initialize WOLFSSL_CTX */
    if ((client_ctx = 
        wolfSSL_CTX_new(wolfTLSv1_2_client_method_ex((void *)NULL))) == NULL) {
        printf("ERROR: failed to create WOLFSSL_CTX\n");
        return;
    }

    #ifdef WOLFSSL_RENESAS_TSIP_TLS
    tsip_set_callbacks(client_ctx);
    #endif

    #if defined(NO_FILESYSTEM)
    
    if (wolfSSL_CTX_load_verify_buffer(client_ctx, cert, 
                            SIZEOF_CERT, SSL_FILETYPE_ASN1) != SSL_SUCCESS) {
           printf("ERROR: can't load certificate data\n");
       return;
    }
    
    #else
    
    if (wolfSSL_CTX_load_verify_locations(client_ctx, cert, 0) != SSL_SUCCESS) {
        printf("ERROR: can't load \"%s\"\n", cert);
        return NULL;
    }
    
    #endif


    /* use specific cipher */
    if (cipherlist != NULL && 
        wolfSSL_CTX_set_cipher_list(client_ctx, cipherlist) != 
                                                            WOLFSSL_SUCCESS) {
        wolfSSL_CTX_free(client_ctx); client_ctx = NULL;
        printf("client can't set cipher list");
    }
}

static void Tls_client()
{
    #define BUFF_SIZE 256
    #define ADDR_SIZE 16
    int             ret;
    WOLFSSL_CTX*    ctx = (WOLFSSL_CTX *)client_ctx;
    WOLFSSL*        ssl;
    Socket_t        socket;
    socklen_t       socksize = sizeof(struct freertos_sockaddr);
    struct freertos_sockaddr    PeerAddr;
    char    addrBuff[ADDR_SIZE] = {0};
  
    static const char sendBuff[]= "Hello Server\n" ;    
    char    rcvBuff[BUFF_SIZE] = {0};


    /* create TCP socket */

    socket = FreeRTOS_socket(FREERTOS_AF_INET,
                             FREERTOS_SOCK_STREAM,
                             FREERTOS_IPPROTO_TCP);

    configASSERT(socket != FREERTOS_INVALID_SOCKET);

    FreeRTOS_bind(socket, NULL, socksize);

    /* attempt to connect TLS server */

    PeerAddr.sin_addr = FreeRTOS_inet_addr(TLSSERVER_IP);
    PeerAddr.sin_port = FreeRTOS_htons(TLSSERVER_PORT);

    ret = FreeRTOS_connect(socket, &PeerAddr, sizeof(PeerAddr));

    if (ret != 0) {
        printf("ERROR FreeRTOS_connect: %d\n",ret);
    }

    /* create WOLFSSL object */
    if (ret == 0) {
        ssl = wolfSSL_new(ctx);
        if (ssl == NULL) {
            printf("ERROR wolfSSL_new: %d\n", wolfSSL_get_error(ssl, 0));
            ret = -1;
        }
    }
    if (ret == 0) {
        #ifdef WOLFSSL_RENESAS_TSIP_TLS
        tsip_set_callback_ctx(ssl, &userContext);
        #endif
    }

    if (ret == 0) {
        /* associate socket with ssl object */
        if (wolfSSL_set_fd(ssl, (int)socket) != WOLFSSL_SUCCESS) {
            printf("ERROR wolfSSL_set_fd: %d\n", wolfSSL_get_error(ssl, 0));
            ret = -1;
        }
    }

    if (ret == 0) {
        if (wolfSSL_connect(ssl) != WOLFSSL_SUCCESS) {
            printf("ERROR wolfSSL_connect: %d\n", wolfSSL_get_error(ssl, 0));
            ret = -1;
        }
    }

    if (ret == 0) {
        if (wolfSSL_write(ssl, sendBuff, strlen(sendBuff)) != 
                                                            strlen(sendBuff)) {
            printf("ERROR wolfSSL_write: %d\n", wolfSSL_get_error(ssl, 0));
            ret = -1;
        }
    }

    if (ret == 0) {
        if ((ret=wolfSSL_read(ssl, rcvBuff, BUFF_SIZE -1)) < 0) {
            printf("ERROR wolfSSL_read: %d\n", wolfSSL_get_error(ssl, 0));
            ret = -1;
        }
        else {
            rcvBuff[ret] = '\0';
            printf("Received: %s\n\n", rcvBuff);
            ret = 0;
        }
    }

    
    wolfSSL_shutdown(ssl);

    FreeRTOS_shutdown(socket, FREERTOS_SHUT_RDWR);
    while(FreeRTOS_recv(socket, rcvBuff, BUFF_SIZE -1, 0) >=0) {
    	vTaskDelay(250);
    }

    FreeRTOS_closesocket(socket);


    wolfSSL_free(ssl);
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();

    return;
}

static void Tls_client_demo(void)
{

    /* setup ciphersuite list to use for TLS handshake */

#if defined(WOLFSSL_RENESAS_TSIP_TLS)

    #ifdef USE_ECC_CERT
    const char* cipherlist[] = {
        "ECDHE-ECDSA-AES128-GCM-SHA256",
        "ECDHE-ECDSA-AES128-SHA256"
    };
    const int cipherlist_sz = 2;

    #else
    const char* cipherlist[] = {
        "ECDHE-RSA-AES128-GCM-SHA256",
        "ECDHE-RSA-AES128-SHA256",
        "AES128-SHA",
        "AES128-SHA256",
        "AES256-SHA",
        "AES256-SHA256"
    };
    const int cipherlist_sz = 6;

    #endif

#else
    const char* cipherlist[] = { NULL };
    const int cipherlist_sz = 0;

#endif

    int i = 0;

    printf("/*------------------------------------------------*/\n");
    printf("    TLS_Client demo\n");
    printf("    - TLS server address:" TLSSERVER_IP " port: %d\n",
                                                             TLSSERVER_PORT);

#if defined(WOLFSSL_RENESAS_TSIP_TLS) && (WOLFSSL_RENESAS_TSIP_VER >=109)
    printf("    - with TSIP\n");
#endif
    printf("/*------------------------------------------------*/\n");

    /* setup credentials for TLS handshake */
#if defined(WOLFSSL_RENESAS_TSIP_TLS) && (WOLFSSL_RENESAS_TSIP_VER >=109)

    #if defined(USE_ECC_CERT)

    /* Root CA cert has ECC-P256 public key */
    tsip_inform_cert_sign((const byte*)ca_ecc_cert_der_sig);

    #else
    
    /* Root CA cert has RSA public key */
    tsip_inform_cert_sign((const byte*)ca_cert_der_sig);

    #endif

    wc_tsip_inform_user_keys_ex(
            (byte*)&g_key_block_data.encrypted_provisioning_key,
            (byte*)&g_key_block_data.iv,
            (byte*)&g_key_block_data.encrypted_user_rsa2048_ne_key,
            encrypted_user_key_type);

    guser_PKCbInfo.user_key_id = 0;

#endif /* WOLFSSL_RENESAS_TSIP_TLS && (WOLFSSL_RENESAS_TSIP_VER >=109) */

    do {
        if(cipherlist_sz > 0 ) printf("cipher : %s\n", cipherlist[i]);

        Tls_client_init(cipherlist[i]);

        Tls_client();

        i++;
    } while (i < cipherlist_sz);

    printf("End of TLS_Client demo.\n");
}


/* Demo entry function called by iot_demo_runner
 * To run this entry function as an aws_iot_demo, define this as 
 * DEMO_entryFUNCTION in aws_demo_config.h.
 */
void wolfSSL_demo_task(bool         awsIotMqttMode,
                       const char*  pIdentifier,
                       void*        pNetworkServerInfo,
                       void*        pNetworkCredentialInfo,
                       const IotNetworkInterface_t* pNetworkInterface)
{

    (void)awsIotMqttMode;
    (void)pIdentifier;
    (void)pNetworkServerInfo;
    (void)pNetworkCredentialInfo;
    (void)pNetworkInterface;


#if defined(CRYPT_TEST)

    CryptTest_demo();

#elif defined(BENCHMARK)

    Benchmark_demo();

#elif defined(TLS_CLIENT)

    Tls_client_demo();

#endif

    while (1) {
        vTaskDelay(10000);
    }
}


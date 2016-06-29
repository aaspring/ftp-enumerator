#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <jansson.h>

#include <arpa/inet.h>

#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/asn1.h>
#include <openssl/objects.h>

#define DATE_LEN 128
#define SIG_ALGO_LEN 64
#define PUBKEY_ALGO_LEN 64
#define EXP_E_STR_SIZE 20
#define MOD_N_STR_SIZE 16384

void convert_ASN1TIME_to_string(ASN1_TIME* asnTime, char* buffer, size_t len) {
	int rc = 0;
	BIO* bio = BIO_new(BIO_s_mem());
	rc = ASN1_TIME_print(bio, asnTime);
	assert(rc > 0);
	rc = BIO_gets(bio, buffer, len);
	assert(rc > 0);
	BIO_free(bio);
}

void sort_cert_stack(STACK_OF(X509)* unsortedStack, STACK_OF(X509)* sortedStack) {
	int i = 0;
	int k = 0;

	sk_X509_push(sortedStack, sk_X509_value(unsortedStack, 0));

	for (i = 1; i < sk_X509_num(unsortedStack); i++) {
		X509* prev = sk_X509_value(sortedStack, i - 1);
		X509* next = NULL;
		for (k = 1; k < sk_X509_num(unsortedStack); k++) {
			X509* cand = sk_X509_value(unsortedStack, k);
			if (
					(!X509_NAME_cmp(cand->cert_info->subject, prev->cert_info->issuer))
					|| (k == sk_X509_num(unsortedStack) - 1)
			) {
				next = cand;
				break;
			}
		}

		if (next) {
			sk_X509_push(sortedStack, next);
		}
		else {
			// Can't sort, leave un-sorted
			sk_X509_free(sortedStack);
			sortedStack = sk_X509_dup(unsortedStack);
			printf("Can't sort the stack, abandoning all hope\n");
		}
	}
}

void get_sig_name(X509* leafCert, char* strBuffer) {
	int pkeyNId = OBJ_obj2nid(leafCert->cert_info->key->algor->algorithm);
	assert (pkeyNId != NID_undef);
	const char* nameBuf = OBJ_nid2ln(pkeyNId);
	assert(strlen(nameBuf) <= SIG_ALGO_LEN);
	strncpy(strBuffer, nameBuf, SIG_ALGO_LEN);
}

void get_pub_key(
								X509* leafCert,
								char* pubKeyAlgStr,
								char* expEHex,
								char* modNHex,
								int* numBits
) {
	int pubKeyAlgNId = OBJ_obj2nid(leafCert->cert_info->key->algor->algorithm);
	assert (pubKeyAlgNId != NID_undef);
	const char* nameBuff = OBJ_nid2ln(pubKeyAlgNId);
	assert(strlen(nameBuff) <= PUBKEY_ALGO_LEN);
	strncpy(pubKeyAlgStr, nameBuff, PUBKEY_ALGO_LEN);

	if (
			(pubKeyAlgNId != NID_rsaEncryption)
			&& (pubKeyAlgNId != NID_X9_62_id_ecPublicKey)
			&& (pubKeyAlgNId != NID_dsa)
	) {
		printf("non-RSA/-ECC/-DSA key found. Type : %d\n", pubKeyAlgNId);
		sprintf(expEHex, "UNK N-Id : %d", pubKeyAlgNId);
		sprintf(modNHex, "UNK N-Id : %d", pubKeyAlgNId);
		return;
	}

	if (pubKeyAlgNId == NID_X9_62_id_ecPublicKey) {
		sprintf(expEHex, "ECC key");
		sprintf(modNHex, "ECC key");
		return;
	}

	if (pubKeyAlgNId == NID_dsa) {
		sprintf(expEHex, "DSA key");
		sprintf(modNHex, "DSA key");
		return;
	}


	EVP_PKEY* pKey = X509_get_pubkey(leafCert);
	assert(pKey);

	RSA* rsaKey;
	char* expEPtr = NULL;
	char* modNPtr = NULL;

	rsaKey = pKey->pkey.rsa;
	assert(rsaKey);

	expEPtr = BN_bn2hex(rsaKey->e);
	assert(strlen(expEPtr) <= EXP_E_STR_SIZE);
	snprintf(expEHex, EXP_E_STR_SIZE, "0x%s", expEPtr);
	OPENSSL_free(expEPtr);

	modNPtr= BN_bn2hex(rsaKey->n);
	assert(strlen(modNPtr) <= MOD_N_STR_SIZE);
	snprintf(modNHex, MOD_N_STR_SIZE, "0x%s", modNPtr);
	OPENSSL_free(modNPtr);

	*numBits = (strlen(modNHex) - 2) / 2 * 8;

	EVP_PKEY_free(pKey);
}

char* get_subj_cn(X509* peerCert) {
	X509_NAME* subjectName = X509_get_subject_name(peerCert);
	assert(subjectName);
	int index = X509_NAME_get_index_by_NID(subjectName, NID_commonName, -1);
	X509_NAME_ENTRY* entry = X509_NAME_get_entry(subjectName, index);
	if (entry) {
		return (char*) ASN1_STRING_data(X509_NAME_ENTRY_get_data(entry));
	}
	else {
		return "NO SUBJECT CN";
	}
}

char* get_issuer_cn(X509* peerCert) {
	X509_NAME* issuer = X509_get_issuer_name(peerCert);
	assert(issuer);
	int index = X509_NAME_get_index_by_NID(issuer, NID_commonName, -1);
	X509_NAME_ENTRY* entry = X509_NAME_get_entry(issuer, index);
	if (entry) {
		return (char*) ASN1_STRING_data(X509_NAME_ENTRY_get_data(entry));
	}
	else {
		return "NO ISSUER CN";
	}
}

void do_work(char* line, X509_STORE* caStore) {
	int rc = 0;
	char notBeforeStr[DATE_LEN];
	char notAfterStr[DATE_LEN];
	char* peerSubj = NULL;
	char* peerIssuer = NULL;
	char* chainUp = NULL;
	bool isCA = false;
	STACK_OF(X509)* unsortedStack = sk_X509_new_null();
	STACK_OF(X509)* sortedStack = sk_X509_new_null();
	char signatureAlgName[SIG_ALGO_LEN + 1];
	char pubKeyAlgStr[PUBKEY_ALGO_LEN + 1];
	char expEHex[EXP_E_STR_SIZE + 1];
	char modNHex[MOD_N_STR_SIZE + 1];
	int numBits = 0;
	char* subjCNStr = NULL;
	char* issuerCNStr = NULL;
	json_error_t jsonError;
	json_t* inObj = NULL;
	int i = 0;

	inObj = json_loads(line, 0, &jsonError);

	const char* ip = json_string_value(json_object_get(inObj, "IP"));

	const char* peerPemCert = json_string_value(json_object_get(inObj, "PEER_CERT"));
	BIO* certBio = BIO_new(BIO_s_mem());
	BIO_write(certBio, peerPemCert, strlen(peerPemCert));
	X509* peerCert = PEM_read_bio_X509(certBio, NULL, NULL, NULL);
	assert(peerCert);
	BIO_free(certBio);
	certBio = NULL;

	peerSubj = X509_NAME_oneline(X509_get_subject_name(peerCert), NULL, 0);
	peerIssuer = X509_NAME_oneline(X509_get_issuer_name(peerCert), NULL, 0);

	ASN1_TIME* notBefore = X509_get_notBefore(peerCert);
	convert_ASN1TIME_to_string(notBefore, notBeforeStr, DATE_LEN);

	ASN1_TIME* notAfter = X509_get_notAfter(peerCert);
	convert_ASN1TIME_to_string(notAfter, notAfterStr, DATE_LEN);

	subjCNStr = get_subj_cn(peerCert);
	issuerCNStr = get_issuer_cn(peerCert);

	if (X509_check_ca(peerCert)) {
		isCA = true;
	}
	else {
		isCA = false;
	}

	json_t* chainArray = json_object_get(inObj, "CERT_CHAIN");
	size_t chainLen = json_array_size(chainArray);
	for (i = 0; i < chainLen; i++) {
		const char* chainPemCert = json_string_value(json_array_get(chainArray, i));
		BIO* chainCertBio = BIO_new(BIO_s_mem());
		BIO_write(chainCertBio, chainPemCert, strlen(chainPemCert));
		X509* chainCert = PEM_read_bio_X509(chainCertBio, NULL, NULL, NULL);
		assert(chainCert);
		sk_X509_push(unsortedStack, chainCert);
		BIO_free(chainCertBio);
		chainCertBio = NULL;
	}
	assert(peerCert != NULL);

	sort_cert_stack(unsortedStack, sortedStack);

	X509_STORE_CTX* ctx = X509_STORE_CTX_new();
	assert(ctx);

	rc = X509_STORE_CTX_init(ctx, caStore, peerCert, sortedStack);
	assert(rc == 1);

	rc = X509_verify_cert(ctx);
	if (rc) {
		chainUp = "VALID";
	}
	else if (X509_check_issued(peerCert, peerCert) == X509_V_OK) {
		chainUp = "SELF-SIGNED";
	}
	else {
		chainUp = "UNK";
	}

	get_sig_name(peerCert, signatureAlgName);

	get_pub_key(peerCert, pubKeyAlgStr, expEHex, modNHex, &numBits);

	json_t* outObj = json_object();
	json_object_set_new(outObj, "IP", json_string(ip));
	json_object_set_new(outObj, "peerSubj", json_string(peerSubj));
	json_object_set_new(outObj, "peerIssuer", json_string(peerIssuer));
	json_object_set_new(outObj, "notBefore", json_string(notBeforeStr));
	json_object_set_new(outObj, "notAfter", json_string(notAfterStr));
	if (isCA) {
		json_object_set_new(outObj, "isCA", json_true());
	}
	else {
		json_object_set_new(outObj, "isCA", json_false());
	}
	json_object_set_new(outObj, "chainUp", json_string(chainUp));
	json_object_set_new(outObj, "algName", json_string(signatureAlgName));
	json_object_set_new(outObj, "expE", json_string(expEHex));
	json_object_set_new(outObj, "modN", json_string(modNHex));
	json_object_set_new(outObj, "numBits", json_integer(numBits));
	json_object_set_new(outObj, "subjCN", json_string(subjCNStr));
	json_object_set_new(outObj, "issuerCN", json_string(issuerCNStr));
	char* jsonStr = json_dumps(outObj, 0);
	printf("%s\n", jsonStr);
	free(jsonStr);

	sk_X509_pop_free(unsortedStack, X509_free);
	sk_X509_free(sortedStack); // Don't pop_free b/c same mallocs as unsorted
	X509_free(peerCert);
	X509_STORE_CTX_free(ctx);
	OPENSSL_free(peerSubj);
	OPENSSL_free(peerIssuer);
	json_decref(inObj);
	json_decref(outObj);
}

X509_STORE* build_ca_cert_store(char* storePath) {
	X509_STORE* caStore = X509_STORE_new();
	assert(caStore);
	int rc = X509_STORE_load_locations(caStore, storePath, NULL);
	assert(rc == 1);
	return caStore;
}

int main(int argc, char** argv) {
	char* line = malloc(1024 * 1024 * 100);
	size_t lineSize = 1024 * 1024 * 100;
	char* storePath = NULL;
	size_t rc = 0;

	bool s = false;
	int opt = 0;
	while ((opt = getopt(argc, argv, "s:")) != -1) {
		switch (opt) {
		case ('s') :
				storePath = optarg;
				s = true;
				break;
		}
	}
	if (!s) {
		printf("You must supply a cert store w/ '-s'\n");
		exit(1);
	}
	X509_STORE* caStore = build_ca_cert_store(storePath);

	while (true) {
		rc = getline(&line, &lineSize, stdin);
		if (rc == -1) {
			break;
		}
		if (strcmp(line, "DONE\n") == 0) {
			break;
		}
		do_work(line, caStore);
	}
	free(line);
	X509_STORE_free(caStore);
}


#include <curl/curl.h>

// XXX should be non-blocking.  maybe use curl_multi_XXX

int
fetch(const char *url, size_t (*callback)(char *, size_t, size_t,
	void *), void *callback_data)
{
	CURL *curl = curl_easy_init();
	CURLcode ret;

	if (curl == NULL) {
		return 1;
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (callback) {
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
	}
	if (callback_data) {
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, callback_data);
	}
	ret = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (ret != CURLE_OK) {
		return 1;
	}
	return 0;
}

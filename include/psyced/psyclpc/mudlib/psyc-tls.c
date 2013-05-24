#include <net.h> // vim syntax=lpc

/* Wrapper for using the tls_check_certificate() efun.
 *
 * If for example the efun returns this raw data:
 * ({ 
 *    0,
 *    ({ 
 *       "2.5.4.3",
 *       "commonName",
 *       "goodadvice.pages.de"
 *    }),
 *    ({ 
 *       0,
 *       0,
 *       0,
 *       0,
 *       0,
 *       0,
 *       0,
 *       0,
 *       0,
 *       0,
 *       0,
 *       0,
 *       "2.5.29.17",
 *       "X509v3 Subject Alternative Name",
 *       ({ 
 *          "dNSName",
 *          "dNSName",
 *          "goodadvice.pages.de",
 *          "1.3.6.1.5.5.7.8.5",
 *          "1.3.6.1.5.5.7.8.5",
 *          "goodadvice.pages.de"
 *       })
 *    })
 * })
 *
 * the wrapper will transform it into:
 *   ([
 *      0: 0
 *      "2.5.29.17:dNSName": "goodadvice.pages.de",
 *      "2.5.4.3": "goodadvice.pages.de",
 *      "2.5.29.17:1.3.6.1.5.5.7.8.5": "goodadvice.pages.de",
 *   ])*
 */

mapping tls_certificate(object who, int longnames) {
    mixed *extra, extensions;
    mapping cert;
    int i, j;

    cert = ([ ]);
#if __EFUN_DEFINED__(tls_check_certificate)
# ifdef WANT_S2S_SASL
    extra = tls_check_certificate(who, 1);
    unless (extra) return 0;
    cert[0] = extra[0];
    extensions = extra[2];
    extra = extra[1];

    for (i = 0; i < sizeof(extra); i += 3) {
	mixed t;

	t = cert[extra[i]];
	unless (t) {
	    cert[extra[i]] = extra[i+2];
	} else if (stringp(t)) {
	    cert[extra[i]] = ({ t, extra[i+2] });
	} else if (pointerp(t)) {
	    cert[extra[i]] += ({ extra[i+2] });
	} else { 
	    // should not happen
	}
    }
    if (longnames) {
	// set up short/long names
	for (i = 0; i < sizeof(extra); i +=3) { 
	    cert[extra[i+1]] = cert[extra[i]];
	}
    }
    for (i = 0; i < sizeof(extensions); i += 3) {
	string key, mkey;
	mixed *val;

	unless(extensions[i]) continue;
	key = extensions[i];
	val = extensions[i+2];
	for (j = 0; j < sizeof(val); j += 3) {
	    mixed t;

	    mkey = key + ":" + val[j];
	    t = cert[mkey];
	    unless (t) {
		cert[mkey] = val[j+2];
	    } else if (stringp(t)) {
		cert[mkey] = ({ t, val[j+2] });
	    } else if (pointerp(t)) {
		cert[mkey] += ({ val[j+2] });
	    } else {
		// should not happen
	    }
	}
    }
# endif
#endif
    return cert;
}

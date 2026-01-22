package chipper

import (
	"crypto/x509"
	"fmt"
	"io/ioutil"
	"log"

	"github.com/gwatts/rootcerts"
)

const (
	localCertFile = "/etc/ssl/certs/local/root.crt"
)

func getTLSCerts() *x509.CertPool {

	pool := rootcerts.ServerCertPool()

	certs, err := ioutil.ReadFile(localCertFile)
	if err != nil {
		fmt.Println("no local cert file")
		return pool
	}

	if ok := pool.AppendCertsFromPEM(certs); !ok {
		log.Println("no local certs appended, using system certs only")
	}

	return pool

}

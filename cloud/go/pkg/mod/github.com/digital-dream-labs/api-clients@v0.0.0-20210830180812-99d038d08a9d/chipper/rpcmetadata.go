package chipper

import "context"

type rpcMetadata struct {
	key  string
	opts *connOpts
}

func (r *rpcMetadata) GetRequestMetadata(ctx context.Context, uri ...string) (map[string]string, error) {
	ret := make(map[string]string)
	addFrom := func(m map[string]string) {
		for k, v := range m {
			ret[k] = v
		}
	}
	if r.opts.creds != nil {
		subdata, err := r.opts.creds.GetRequestMetadata(ctx, uri...)
		if err != nil {
			return nil, err
		}
		addFrom(subdata)
	}
	addFrom(map[string]string{
		"trusted_app_key": r.key,
		"device-id":       r.opts.deviceID,
		"session-id":      r.opts.sessionID,
	})
	return ret, nil
}

func (r *rpcMetadata) RequireTransportSecurity() bool {
	return true
}

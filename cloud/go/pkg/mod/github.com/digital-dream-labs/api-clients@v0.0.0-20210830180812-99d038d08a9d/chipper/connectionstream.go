package chipper

import pb "github.com/digital-dream-labs/api/go/chipperpb"

type connectionStream struct {
	baseStream
	opts *ConnectOpts
}

func (c *connectionStream) SendAudio(audioData []byte) error {
	return c.sendAudio(audioData, c.createMessage)
}
func (c *connectionStream) createMessage(audioData []byte, encoding pb.AudioEncoding) interface{} {
	ret := new(pb.StreamingConnectionCheckRequest)
	if !c.hasStreamed {
		*ret = pb.StreamingConnectionCheckRequest{
			DeviceId:        c.conn.opts.deviceID,
			Session:         c.conn.opts.sessionID,
			AppKey:          c.conn.appKey,
			TotalAudioMs:    c.opts.TotalAudioMs,
			AudioPerRequest: c.opts.AudioPerRequestMs,
			FirmwareVersion: c.conn.opts.firmware,
		}
	}
	ret.InputAudio = audioData
	return ret
}

func (c *connectionStream) WaitForResponse() (interface{}, error) {
	response := new(ConnectionCheckResponse)
	err := c.client.RecvMsg(response)
	if err != nil {
		return nil, err
	}
	return response, nil
}

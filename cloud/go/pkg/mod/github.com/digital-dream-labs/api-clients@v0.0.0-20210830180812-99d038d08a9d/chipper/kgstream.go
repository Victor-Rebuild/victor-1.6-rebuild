package chipper

import pb "github.com/digital-dream-labs/api/go/chipperpb"

type kgStream struct {
	baseStream
	opts *KGOpts
}

func (c *kgStream) SendAudio(audioData []byte) error {
	return c.sendAudio(audioData, c.createMessage)
}

func (c *kgStream) createMessage(audioData []byte, encoding pb.AudioEncoding) interface{} {
	ret := new(pb.StreamingKnowledgeGraphRequest)
	if !c.hasStreamed {
		*ret = pb.StreamingKnowledgeGraphRequest{
			DeviceId:        c.conn.opts.deviceID,
			Session:         c.conn.opts.sessionID,
			LanguageCode:    c.opts.Language,
			AppKey:          c.conn.appKey,
			AudioEncoding:   encoding,
			SaveAudio:       c.opts.SaveAudio,
			FirmwareVersion: c.conn.opts.firmware,
			BootId:          c.conn.opts.bootID,
			SkipDas:         c.opts.NoDas,
		}
	}
	ret.InputAudio = audioData
	return ret
}

func (c *kgStream) WaitForResponse() (interface{}, error) {
	response := new(KnowledgeGraphResponse)
	err := c.client.RecvMsg(response)
	if err != nil {
		return nil, err
	}
	return response, nil
}

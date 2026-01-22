package chipper

import pb "github.com/digital-dream-labs/api/go/chipperpb"

type intentGraphStream struct {
	baseStream
	opts *IntentGraphOpts
}

func (c *intentGraphStream) SendAudio(audioData []byte) error {
	return c.sendAudio(audioData, c.createMessage)
}

func (c *intentGraphStream) createMessage(audioData []byte, encoding pb.AudioEncoding) interface{} {
	ret := new(pb.StreamingIntentGraphRequest)
	if !c.hasStreamed {
		*ret = pb.StreamingIntentGraphRequest{
			DeviceId:        c.conn.opts.deviceID,
			Session:         c.conn.opts.sessionID,
			LanguageCode:    c.opts.Language,
			AppKey:          c.conn.appKey,
			AudioEncoding:   encoding,
			SaveAudio:       c.opts.SaveAudio,
			FirmwareVersion: c.conn.opts.firmware,
			BootId:          c.conn.opts.bootID,
			SkipDas:         c.opts.NoDas,
			// Intent-Specific
			SingleUtterance: false,
			IntentService:   c.opts.Handler,
			SpeechOnly:      c.opts.SpeechOnly,
			Mode:            c.opts.Mode,
		}
	}
	ret.InputAudio = audioData
	return ret
}

func (c *intentGraphStream) WaitForResponse() (interface{}, error) {
	for {
		intentG := new(IntentGraphResponse)
		err := c.client.RecvMsg(intentG)
		if err != nil {
			return nil, err
		} else if !intentG.GetIsFinal() {
			// ignore non-final responses
			continue
		}

		return intentG, nil
	}
}

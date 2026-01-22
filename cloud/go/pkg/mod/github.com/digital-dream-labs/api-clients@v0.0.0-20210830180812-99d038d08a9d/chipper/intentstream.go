package chipper

import pb "github.com/digital-dream-labs/api/go/chipperpb"

type intentStream struct {
	baseStream
	opts *IntentOpts
}

func (c *intentStream) SendAudio(audioData []byte) error {
	return c.sendAudio(audioData, c.createMessage)
}

func (c *intentStream) createMessage(audioData []byte, encoding pb.AudioEncoding) interface{} {
	ret := new(pb.StreamingIntentRequest)
	if !c.hasStreamed {
		*ret = pb.StreamingIntentRequest{
			DeviceId:        c.conn.opts.deviceID,
			Session:         c.conn.opts.sessionID,
			LanguageCode:    c.opts.Language,
			SingleUtterance: false,
			IntentService:   c.opts.Handler,
			AppKey:          c.conn.appKey,
			AudioEncoding:   encoding,
			SpeechOnly:      c.opts.SpeechOnly,
			Mode:            c.opts.Mode,
			SaveAudio:       c.opts.SaveAudio,
			FirmwareVersion: c.conn.opts.firmware,
			BootId:          c.conn.opts.bootID,
			SkipDas:         c.opts.NoDas,
		}
	}
	ret.InputAudio = audioData
	return ret
}

func (c *intentStream) WaitForResponse() (interface{}, error) {
	for {
		intent := new(IntentResponse)
		err := c.client.RecvMsg(intent)
		if err != nil {
			return nil, err
		} else if !intent.GetIsFinal() {
			// ignore non-final responses
			continue
		}
		return intent.GetIntentResult(), nil
	}
}

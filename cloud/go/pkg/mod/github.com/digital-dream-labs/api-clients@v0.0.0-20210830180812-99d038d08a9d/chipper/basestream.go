package chipper

import (
	pb "github.com/digital-dream-labs/api/go/chipperpb"
	"github.com/digital-dream-labs/opus-go/opus"
	"google.golang.org/grpc"
)

type baseStream struct {
	conn        *Conn
	opts        *StreamOpts
	encoder     *opus.OggStream
	client      grpc.ClientStream
	hasStreamed bool
	cancel      func()
}

func (c *baseStream) sendAudio(audioData []byte, messageFunc func([]byte, pb.AudioEncoding) interface{}) error {
	encoding := pb.AudioEncoding_LINEAR_PCM

	if c.opts.Compress {
		encoding = pb.AudioEncoding_OGG_OPUS
		var err error
		audioData, err = c.encoder.EncodeBytes(audioData)
		if err != nil {
			return err
		}
		flushData := c.encoder.Flush()
		audioData = append(audioData, flushData...)
	} else if c.opts.PreEncoded {
		encoding = pb.AudioEncoding_OGG_OPUS
	}

	toSend := messageFunc(audioData, encoding)
	c.hasStreamed = true
	return c.client.SendMsg(toSend)
}

func (c *baseStream) Close() error {
	if c.cancel != nil {
		c.cancel()
	}
	return c.client.CloseSend()
}

func (c *baseStream) CloseSend() error {
	return c.client.CloseSend()
}

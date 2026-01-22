package chipper

import (
	"context"
	"fmt"
	"time"

	pb "github.com/digital-dream-labs/api/go/chipperpb"
	"github.com/digital-dream-labs/opus-go/opus"
	"github.com/grpc-ecosystem/grpc-opentracing/go/otgrpc"
	"github.com/opentracing/opentracing-go"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
)

// Stream represents an open stream of various possible types (intent, knowledge graph) to Chipper
type Stream interface {
	SendAudio(audioData []byte) error
	WaitForResponse() (interface{}, error)
	Close() error
	CloseSend() error
}

// StreamOpts defines options common to all possible stream types
type StreamOpts struct {
	CompressOpts
	Timeout   time.Duration
	Language  pb.LanguageCode
	SaveAudio bool
	NoDas     bool
}

// IntentOpts extends StreamOpts with options unique to intent streams
type IntentOpts struct {
	StreamOpts
	Handler    pb.IntentService
	Mode       pb.RobotMode
	SpeechOnly bool
}

// ConnectOpts extends StreamOpts with options unique to connection check streams
type ConnectOpts struct {
	StreamOpts
	TotalAudioMs      uint32
	AudioPerRequestMs uint32
}

// KGOpts extends StreamOpts with options unique to knowledge graph streams
type KGOpts struct {
	StreamOpts
	Timezone string
}

// IntentGraphOpts extends StreamOpts with options of both Intent and KG
type IntentGraphOpts struct {
	StreamOpts
	Handler    pb.IntentService
	Mode       pb.RobotMode
	SpeechOnly bool

	// Doesnt seem to be passing this in
	Timezone string
}

// CompressOpts specifies whether compression should be used and, if so, allows
// the specification of parameters related to it
type CompressOpts struct {
	Bitrate    uint
	Complexity uint
	FrameSize  float32
	PreEncoded bool
	Compress   bool
}

// Conn represents an underlying GRPC connection to the Chipper server
type Conn struct {
	conn   *grpc.ClientConn
	client pb.ChipperGrpcClient
	appKey string
	opts   *connOpts
}

// NewConn creates a new connection to the given GRPC server
func NewConn(ctx context.Context, serverURL string, appKey string, options ...ConnOpt) (*Conn, error) {
	// create default options and apply user provided overrides
	opts := connOpts{deviceID: "device-id"}
	for _, opt := range options {
		opt(&opts)
	}

	dialOpts := opts.grpcOpts
	if opts.insecure {
		dialOpts = append(dialOpts, grpc.WithInsecure())
	} else {
		dialOpts = append(
			dialOpts,
			grpc.WithTransportCredentials(
				credentials.NewClientTLSFromCert(getTLSCerts(), ""),
			),
		)
	}

	if metadata := getMetadata(appKey, &opts); metadata != nil {
		dialOpts = append(dialOpts, grpc.WithPerRPCCredentials(metadata))
	}

	dialOpts = append(dialOpts, grpc.WithUnaryInterceptor(otgrpc.OpenTracingClientInterceptor(opentracing.GlobalTracer())))
	dialOpts = append(dialOpts, grpc.WithStreamInterceptor(otgrpc.OpenTracingStreamClientInterceptor(opentracing.GlobalTracer())))

	rpcConn, err := grpc.DialContext(ctx, serverURL, dialOpts...)
	if err != nil {
		return nil, err
	}

	rpcClient := pb.NewChipperGrpcClient(rpcConn)

	conn := &Conn{
		conn:   rpcConn,
		client: rpcClient,
		appKey: appKey,
		opts:   &opts}
	return conn, nil
}

func (c *Conn) newStream(opts *StreamOpts, client grpc.ClientStream, cancel func()) baseStream {
	var encoder *opus.OggStream
	if opts.Compress {
		encoder = &opus.OggStream{
			SampleRate: 16000,
			Channels:   1,
			FrameSize:  opts.FrameSize,
			Bitrate:    opts.Bitrate,
			Complexity: opts.Complexity}
	}

	return baseStream{
		conn:    c,
		client:  client,
		opts:    opts,
		encoder: encoder,
		cancel:  cancel,
	}
}

// NewIntentStream opens a new stream on the given connection to stream audio for the purpose of getting
// an intent response
func (c *Conn) NewIntentStream(ctx context.Context, opts IntentOpts) (Stream, error) {
	ctx, cancel := getContext(ctx, &opts.StreamOpts)
	client, err := c.client.StreamingIntent(ctx)
	if err != nil {
		fmt.Println("GRPC Stream creation error:", err)
		return nil, err
	}
	return &intentStream{
		baseStream: c.newStream(&opts.StreamOpts, client, cancel),
		opts:       &opts,
	}, nil
}

// NewKGStream opens a new stream on the given connection to stream audio for the purpose of getting
// a knowledge graph response
func (c *Conn) NewKGStream(ctx context.Context, opts KGOpts) (Stream, error) {
	ctx, cancel := getContext(ctx, &opts.StreamOpts)
	client, err := c.client.StreamingKnowledgeGraph(ctx)
	if err != nil {
		fmt.Println("GRPC Stream creation error:", err)
		return nil, err
	}
	return &kgStream{
		baseStream: c.newStream(&opts.StreamOpts, client, cancel),
		opts:       &opts,
	}, nil
}

// NewIntentGraphStream opens a new stream on the given connection to stream audio for the purpose of getting
// a knowledge graph response
func (c *Conn) NewIntentGraphStream(ctx context.Context, opts IntentGraphOpts) (Stream, error) {
	ctx, cancel := getContext(ctx, &opts.StreamOpts)
	client, err := c.client.StreamingIntentGraph(ctx)
	if err != nil {
		return nil, err
	}
	return &intentGraphStream{
		baseStream: c.newStream(&opts.StreamOpts, client, cancel),
		opts:       &opts,
	}, nil
}

// NewConnectionStream opens a stream for doing connection checks
func (c *Conn) NewConnectionStream(ctx context.Context, opts ConnectOpts) (Stream, error) {
	ctx, cancel := getContext(ctx, &opts.StreamOpts)
	client, err := c.client.StreamingConnectionCheck(ctx)
	if err != nil {
		fmt.Println("GRPC Stream creation error:", err)
		return nil, err
	}
	return &connectionStream{
		baseStream: c.newStream(&opts.StreamOpts, client, cancel),
		opts:       &opts,
	}, nil

}

// Close closes the underlying connection
func (c *Conn) Close() error {
	return c.conn.Close()
}

// SendText performs an intent request with a text string instead of voice data
func (c *Conn) SendText(ctx context.Context, text, session, device string,
	service pb.IntentService) *pb.IntentResult {

	r := &pb.TextRequest{
		TextInput:       text,
		DeviceId:        device,
		Session:         session,
		LanguageCode:    pb.LanguageCode_ENGLISH_US,
		IntentService:   service,
		FirmwareVersion: c.opts.firmware,
	}

	res, err := c.client.TextIntent(ctx, r)
	if err != nil {
		fmt.Println("Text intent error:", err)
		return nil
	}
	fmt.Printf("Text_intent=\"%s\"  query=\"%s\", param=\"%v\"",
		res.IntentResult.Action,
		res.IntentResult.QueryText,
		res.IntentResult.Parameters)
	return res.IntentResult
}

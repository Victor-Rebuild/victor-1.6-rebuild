package chipper

import (
	"context"
	"time"

	pb "github.com/digital-dream-labs/api/go/chipperpb"
)

type (
	// IntentResult aliases the protobuf IntentResult type
	IntentResult = pb.IntentResult
	// IntentResponse aliases the protobuf IntentResponse type
	IntentResponse = pb.IntentResponse
	// KnowledgeGraphResponse aliases the protobuf KnowledgeGraphResponse type
	KnowledgeGraphResponse = pb.KnowledgeGraphResponse
	// IntentGraphResponse aliases the protobuf IntentGraphResponse type
	IntentGraphResponse = pb.IntentGraphResponse
	// ConnectionCheckResponse aliases the protobuf ConnectionCheckResponse type
	ConnectionCheckResponse = pb.ConnectionCheckResponse
)

func getMetadata(key string, opts *connOpts) *rpcMetadata {
	return &rpcMetadata{key, opts}
}

func getContext(ctx context.Context, opts *StreamOpts) (context.Context, func()) {
	if opts.Timeout > 0 {
		return context.WithDeadline(ctx, time.Now().Add(opts.Timeout))
	}
	return ctx, nil
}

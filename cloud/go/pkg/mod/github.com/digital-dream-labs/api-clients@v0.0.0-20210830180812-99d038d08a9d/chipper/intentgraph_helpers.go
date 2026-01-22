package chipper

import (
	pb "github.com/digital-dream-labs/api/go/chipperpb"
)

func IsIntent(i IntentGraphResponse) bool {
	return i.ResponseType == pb.IntentGraphMode_INTENT
}

func IsKnowledgeGraph(i IntentGraphResponse) bool {
	return i.ResponseType == pb.IntentGraphMode_INTENT
}

func ConvertToIntentResp(resp *IntentGraphResponse) *IntentResult {
	intentResp := resp.IntentResult

	return intentResp
}

func ConvertToKnowledgeGraphResp(resp *IntentGraphResponse) *KnowledgeGraphResponse {
	kgResp := &KnowledgeGraphResponse{
		Session:     resp.Session,
		DeviceId:    resp.DeviceId,
		QueryText:   resp.QueryText,
		SpokenText:  resp.SpokenText,
		CommandType: resp.CommandType,
		DomainsUsed: resp.DomainsUsed,
		AudioId:     resp.AudioId,
	}

	return kgResp
}

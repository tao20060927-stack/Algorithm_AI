#!/bin/bash
cat << 'EOF'
# TAB Lenses

## Lens Framework

| Lens | Key Questions |
|------|---------------|
| Architecture clarity | Are boundaries clear? Is the design defensible? |
| Vendor portability | Is architecture vendor-coupled? Migration path? |
| Security posture | What assurance evidence? Are controls proportionate? |
| Integration risk | External dependencies? Failure handling? |
| Operability | Can team monitor, debug, recover from incidents? |
| Data integrity | How is consistency maintained? Data lifecycle? |
| Data sources | Mock/synthetic or real? Classification? |
| Accessibility | Required standards met? Testing approach? |
| Delivery feasibility | Realistic scope? Technical debt contained? |
| Scalability claims | Backed by concrete examples? |

## For Each Concern Document

1. **What** - The concern in TAB language
2. **Why** - Impact if not addressed
3. **Current state** - Evidence or gap
4. **Mitigation** - Recommended action
5. **TAB Ask** - What approval/condition needed

## Common TAB Questions

**Architecture:** Why this tech? Alternatives considered? Integration with existing? Migration/rollback strategy?

**Vendor Portability:** Vendor lock-in? Switch requirements? Abstraction layer? Market volatility accommodation?

**Security:** AuthN method? Data storage/protection? External service calls? Security testing done?

**Operability:** How know if broken? Incident response? Out-of-hours support? Deployment/rollback process?

**Data:** Personal data processed? Data flows? Retention policy? Backup approach?

**Data Sources:** Sources used? Mock or real? Classification? Handling requirements?

## Risk Framing

Frame risks as conditions: "Approve with condition that X is implemented before Y" rather than "Cannot approve".
EOF

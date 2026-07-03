#!/bin/bash
cat << 'EOF'
# Assessment Checklists

Complete each with evidence locations or mark gaps.

## Security Checklist

| Control | What to Look For | Evidence |
|---------|------------------|----------|
| AuthN/AuthZ | User/service auth; authorisation model | |
| CSRF protection | Token-based on state-changing forms | |
| Cookie settings | Secure, HttpOnly, SameSite flags | |
| Security headers | CSP, X-Frame-Options, Helmet | |
| Secrets management | No secrets in repo; env vars or vault | |
| External auth | S2S tokens, OAuth, API keys management | |
| PII handling | Logging redaction, data minimisation | |
| Input validation | Sanitisation, injection prevention | |
| Dependencies | CVE tracking and remediation | |

## Operability Checklist

| Capability | What to Look For | Evidence |
|------------|------------------|----------|
| Health endpoints | Meaningful checks (not just "200 OK") | |
| Structured logging | JSON logs with consistent fields | |
| Correlation IDs | Request tracing across services | |
| Metrics | What's measured, where exposed | |
| Alerting | Triggers, recipients | |
| Failure modes | Behaviour when dependencies fail | |
| Runbooks | Incident response procedures | |
| Deployment | Release process, rollback approach | |

## Testing Checklist

| Test Type | What to Look For | Evidence |
|-----------|------------------|----------|
| Unit tests | Coverage of key logic; location | |
| Integration | Database, external service mocking | |
| Contract | Consumer/provider contracts | |
| E2E | Critical journey coverage | |
| Accessibility | Automated a11y; manual testing | |
| Security | SAST/DAST; penetration testing | |
| Performance | Load testing approach | |

## Status Values

- **PROVEN** - Evidence at specified location
- **UNPROVEN** - Claim without evidence
- **GAP** - Known missing capability
- **N/A** - Not applicable for this phase
EOF

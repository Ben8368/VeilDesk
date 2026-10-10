use crate::envelope::{
    AuthorizationEnvelope, Subject, MAX_BYTES, MAX_FRAMES, ROLE_BROKER, SCOPE_RECORD, SCOPE_VIEW,
};
use std::collections::BTreeMap;

pub const PROTOCOL: u32 = 1;
pub const MAX_LEASE_MS: u64 = 30_000;
pub const MAX_LEASES: usize = 16;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct LeaseState {
    envelope: AuthorizationEnvelope,
    last_frame: u32,
    used_bytes: u64,
}

pub struct Policy {
    epoch: u64,
    generation: u64,
    next_lease_id: u64,
    next_nonce: u64,
    leases: BTreeMap<u64, LeaseState>,
}

impl Policy {
    pub fn new(epoch: u64) -> Result<Self, &'static str> {
        if epoch == 0 || epoch == u64::MAX {
            return Err("invalid_epoch");
        }
        Ok(Self {
            epoch,
            generation: 1,
            next_lease_id: 0,
            next_nonce: 0,
            leases: BTreeMap::new(),
        })
    }

    pub fn issue(
        &mut self,
        subject: Subject,
        scope: u32,
        duration_ms: u64,
        now: u64,
        max_frames: u32,
        max_bytes: u32,
    ) -> Result<AuthorizationEnvelope, &'static str> {
        self.leases.retain(|_, lease| now < lease.envelope.deadline);
        let role_scope_valid = matches!(
            (subject.role, scope),
            (ROLE_BROKER, SCOPE_VIEW) | (ROLE_BROKER, SCOPE_RECORD)
        );
        if !role_scope_valid
            || !valid_subject(subject)
            || duration_ms == 0
            || duration_ms > MAX_LEASE_MS
            || max_frames == 0
            || max_frames > MAX_FRAMES
            || max_bytes == 0
            || max_bytes > MAX_BYTES
        {
            return Err("invalid_envelope");
        }
        if self.leases.len() >= MAX_LEASES {
            return Err("budget");
        }
        let deadline = now.checked_add(duration_ms).ok_or("overflow")?;
        let lease_id = self.next_lease_id.checked_add(1).ok_or("overflow")?;
        let nonce = self.next_nonce.checked_add(1).ok_or("overflow")?;
        let envelope = AuthorizationEnvelope {
            lease_id,
            epoch: self.epoch,
            generation: self.generation,
            subject,
            scope,
            issued_at: now,
            deadline,
            nonce,
            max_frames,
            max_bytes,
        };
        if !envelope.valid_fields() {
            return Err("invalid_envelope");
        }
        self.next_lease_id = lease_id;
        self.next_nonce = nonce;
        self.leases.insert(
            lease_id,
            LeaseState {
                envelope,
                last_frame: 0,
                used_bytes: 0,
            },
        );
        Ok(envelope)
    }

    pub fn validate(&self, envelope: &AuthorizationEnvelope, caller: Subject, now: u64) -> bool {
        envelope.valid_fields()
            && envelope.subject == caller
            && envelope.epoch == self.epoch
            && envelope.generation == self.generation
            && now >= envelope.issued_at
            && now < envelope.deadline
            && self
                .leases
                .get(&envelope.lease_id)
                .is_some_and(|lease| lease.envelope == *envelope)
    }

    pub fn record_frames(
        &mut self,
        envelopes: &[(AuthorizationEnvelope, u32)],
        frame: u32,
        caller: Subject,
        now: u64,
    ) -> Result<(), &'static str> {
        if envelopes.is_empty() || envelopes.len() > 2 || frame == 0 {
            return Err("denied");
        }
        for (index, (envelope, byte_count)) in envelopes.iter().enumerate() {
            if !self.validate(envelope, caller, now)
                || envelopes[..index]
                    .iter()
                    .any(|(previous, _)| previous.lease_id == envelope.lease_id)
            {
                return Err("denied");
            }
            let lease = self.leases.get(&envelope.lease_id).ok_or("denied")?;
            if frame != lease.last_frame.checked_add(1).ok_or("budget")? {
                return Err("replay");
            }
            if frame > envelope.max_frames || *byte_count == 0 || *byte_count > envelope.max_bytes {
                return Err("budget");
            }
            if lease
                .used_bytes
                .checked_add(u64::from(*byte_count))
                .is_none_or(|used| used > u64::from(envelope.max_bytes))
            {
                return Err("budget");
            }
        }
        for (envelope, byte_count) in envelopes {
            let lease = self.leases.get_mut(&envelope.lease_id).ok_or("denied")?;
            lease.last_frame = frame;
            lease.used_bytes += u64::from(*byte_count);
        }
        Ok(())
    }

    pub fn revoke_all(&mut self) -> Result<(), &'static str> {
        self.leases.clear();
        self.generation = self.generation.checked_add(1).ok_or("overflow")?;
        Ok(())
    }

    // The public diagnostic pipe still cannot issue grants. Envelope issuance is an
    // internal policy capability until Service peer authentication and Broker IPC land.
    pub fn request(&mut self, bytes: &[u8]) -> String {
        match bytes {
            b"VD1 STATUS\n" => format!(
                "VD1 OK generation={} leases={} grants=disabled\n",
                self.generation,
                self.leases.len()
            ),
            b"VD1 REVOKE\n" => match self.revoke_all() {
                Ok(()) => format!("VD1 OK generation={}\n", self.generation),
                Err(_) => "VD1 DENIED exhausted\n".into(),
            },
            _ => "VD1 DENIED invalid_or_unsupported\n".into(),
        }
    }
}

fn valid_subject(subject: Subject) -> bool {
    subject.pid != 0
        && subject.session != 0
        && subject.creation_time != 0
        && subject.sid_hash.iter().any(|byte| *byte != 0)
        && subject.image_hash.iter().any(|byte| *byte != 0)
        && subject.role == ROLE_BROKER
}

#[cfg(test)]
mod tests {
    use super::*;

    fn viewer() -> Subject {
        Subject {
            pid: 2,
            session: 3,
            creation_time: 4,
            sid_hash: [0x11; 32],
            image_hash: [0x22; 32],
            role: ROLE_BROKER,
        }
    }

    #[test]
    fn service_issues_and_validates_exact_subject_generation_and_deadline() {
        let mut policy = Policy::new(9).unwrap();
        let envelope = policy
            .issue(viewer(), SCOPE_VIEW, 100, 10, 2, 4096)
            .unwrap();
        assert!(policy.validate(&envelope, viewer(), 10));
        assert!(policy.validate(&envelope, viewer(), 109));
        assert!(!policy.validate(&envelope, viewer(), 110));

        let mut forged = envelope;
        forged.subject.pid += 1;
        assert!(!policy.validate(&forged, viewer(), 20));
        forged = envelope;
        forged.generation += 1;
        assert!(!policy.validate(&forged, viewer(), 20));
        forged = envelope;
        forged.epoch += 1;
        assert!(!policy.validate(&forged, viewer(), 20));

        policy.revoke_all().unwrap();
        assert!(!policy.validate(&envelope, viewer(), 20));
    }

    #[test]
    fn frame_accounting_rejects_replay_and_excess_budget() {
        let mut policy = Policy::new(9).unwrap();
        let envelope = policy.issue(viewer(), SCOPE_VIEW, 100, 10, 2, 100).unwrap();
        assert!(policy
            .record_frames(&[(envelope, 50)], 1, viewer(), 10)
            .is_ok());
        assert_eq!(
            policy.record_frames(&[(envelope, 1)], 1, viewer(), 10),
            Err("replay")
        );
        assert_eq!(
            policy.record_frames(&[(envelope, 101)], 2, viewer(), 10),
            Err("budget")
        );
        assert!(policy
            .record_frames(&[(envelope, 50)], 2, viewer(), 10)
            .is_ok());
        assert_eq!(
            policy.record_frames(&[(envelope, 1)], 3, viewer(), 10),
            Err("budget")
        );
    }

    #[test]
    fn frame_accounting_enforces_cumulative_byte_budget_atomically() {
        let mut policy = Policy::new(9).unwrap();
        let view = policy.issue(viewer(), SCOPE_VIEW, 100, 10, 4, 100).unwrap();
        let record = policy
            .issue(viewer(), SCOPE_RECORD, 100, 10, 4, 120)
            .unwrap();
        assert!(policy
            .record_frames(&[(view, 50), (record, 50)], 1, viewer(), 10)
            .is_ok());
        assert!(policy
            .record_frames(&[(view, 10), (record, 10)], 2, viewer(), 10)
            .is_ok());
        assert_eq!(
            policy.record_frames(&[(view, 50), (record, 61)], 3, viewer(), 10),
            Err("budget")
        );
        // The failed two-scope transaction did not consume budget on either lease.
        assert!(policy.record_frames(&[(view, 40)], 3, viewer(), 10).is_ok());
        assert!(policy
            .record_frames(&[(record, 60)], 3, viewer(), 10)
            .is_ok());
    }

    #[test]
    fn restart_discards_envelopes_and_renewal_never_reuses_ids_or_nonce() {
        let mut old = Policy::new(9).unwrap();
        let first = old.issue(viewer(), SCOPE_VIEW, 100, 10, 2, 100).unwrap();
        let second = old.issue(viewer(), SCOPE_VIEW, 100, 10, 2, 100).unwrap();
        assert!(second.lease_id > first.lease_id && second.nonce > first.nonce);
        let restarted = Policy::new(10).unwrap();
        assert!(!restarted.validate(&first, viewer(), 20));
        assert!(Policy::new(0).is_err());
        assert!(Policy::new(u64::MAX).is_err());
    }

    #[test]
    fn role_scope_duration_and_budget_are_bounded() {
        let mut policy = Policy::new(9).unwrap();
        assert!(policy.issue(viewer(), 99, 10, 0, 1, 1).is_err());
        let mut invalid = viewer();
        invalid.role = ROLE_BROKER + 1;
        assert!(policy.issue(invalid, SCOPE_VIEW, 10, 0, 1, 1).is_err());
        assert!(policy.issue(viewer(), SCOPE_VIEW, 0, 0, 1, 1).is_err());
        assert!(policy
            .issue(viewer(), SCOPE_VIEW, MAX_LEASE_MS + 1, 0, 1, 1)
            .is_err());
        assert!(policy
            .issue(viewer(), SCOPE_VIEW, 10, u64::MAX, 1, 1)
            .is_err());
        assert!(policy.issue(viewer(), SCOPE_VIEW, 10, 0, 0, 1).is_err());
        assert!(policy.issue(viewer(), SCOPE_VIEW, 10, 0, 1, 0).is_err());
        assert!(policy
            .issue(viewer(), SCOPE_VIEW, 10, 0, MAX_FRAMES + 1, 1)
            .is_err());
        assert!(policy
            .issue(viewer(), SCOPE_VIEW, 10, 0, 1, MAX_BYTES + 1)
            .is_err());
        for _ in 0..MAX_LEASES {
            policy.issue(viewer(), SCOPE_VIEW, 10, 0, 1, 1).unwrap();
        }
        assert!(policy.issue(viewer(), SCOPE_VIEW, 10, 0, 1, 1).is_err());
    }

    #[test]
    fn public_diagnostic_protocol_still_denies_grants() {
        let mut policy = Policy::new(1).unwrap();
        for bad in [
            b"VD2 STATUS\n".as_slice(),
            b"VD1 GRANT 1\n",
            b"VD1 STATUS\nextra",
            b"VD1 STATUS\r\n",
            b"",
        ] {
            assert!(policy.request(bad).contains("DENIED"));
        }
        assert!(policy.request(b"VD1 STATUS\n").contains("grants=disabled"));
        assert_eq!(policy.request(b"VD1 REVOKE\n"), "VD1 OK generation=2\n");
        assert_eq!(PROTOCOL, 1);
    }
}

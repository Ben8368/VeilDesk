use std::collections::BTreeMap;

pub const PROTOCOL: u32 = 1;
pub const MAX_LEASE_MS: u64 = 30_000;
pub const MAX_LEASES: usize = 16;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Lease {
    pub generation: u64,
    pub session: u32,
    pub expires: u64,
}

pub struct Policy {
    generation: u64,
    next: u64,
    leases: BTreeMap<u64, Lease>,
}

impl Policy {
    pub fn new(epoch: u64) -> Result<Self, &'static str> {
        if epoch == 0 || epoch == u64::MAX {
            return Err("invalid_epoch");
        }
        Ok(Self {
            generation: epoch,
            next: 0,
            leases: BTreeMap::new(),
        })
    }

    pub fn grant(
        &mut self,
        session: u32,
        duration: u64,
        now: u64,
    ) -> Result<(u64, Lease), &'static str> {
        self.leases.retain(|_, lease| now < lease.expires);
        if session == 0 || duration == 0 || duration > MAX_LEASE_MS {
            return Err("invalid_lease");
        }
        if self.leases.len() >= MAX_LEASES {
            return Err("budget");
        }
        let expires = now.checked_add(duration).ok_or("overflow")?;
        self.next = self.next.checked_add(1).ok_or("overflow")?;
        let lease = Lease {
            generation: self.generation,
            session,
            expires,
        };
        self.leases.insert(self.next, lease);
        Ok((self.next, lease))
    }

    pub fn validate(&self, id: u64, generation: u64, session: u32, now: u64) -> bool {
        self.leases.get(&id).is_some_and(|lease| {
            lease.generation == generation && lease.session == session && now < lease.expires
        })
    }

    pub fn revoke_all(&mut self) -> Result<(), &'static str> {
        self.leases.clear();
        self.generation = self.generation.checked_add(1).ok_or("overflow")?;
        Ok(())
    }

    // Public service transport deliberately exposes diagnostics/revoke only.
    // Enrollment and Broker enforcement must be accepted before network grants exist.
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

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn session_generation_expiry_and_revoke() {
        let mut policy = Policy::new(1).unwrap();
        let (id, lease) = policy.grant(2, 100, 10).unwrap();
        assert!(policy.validate(id, lease.generation, 2, 109));
        assert!(!policy.validate(id, lease.generation, 2, 110));
        assert!(!policy.validate(id, lease.generation, 3, 20));
        assert!(!policy.validate(id, lease.generation + 1, 2, 20));
        policy.revoke_all().unwrap();
        assert!(!policy.validate(id, lease.generation, 2, 20));
    }
    #[test]
    fn restart_discards_grants() {
        let mut old = Policy::new(1).unwrap();
        let (id, lease) = old.grant(2, 100, 10).unwrap();
        let mut restarted = Policy::new(2).unwrap();
        restarted.grant(2, 100, 10).unwrap();
        assert!(!restarted.validate(id, lease.generation, 2, 20));
    }
    #[test]
    fn bounded_budget_and_overflow() {
        let mut policy = Policy::new(1).unwrap();
        assert!(Policy::new(0).is_err());
        assert!(policy.grant(0, 10, 0).is_err());
        assert!(policy.grant(1, 0, 0).is_err());
        assert!(policy.grant(1, MAX_LEASE_MS + 1, 0).is_err());
        assert!(policy.grant(1, 10, u64::MAX).is_err());
        for _ in 0..MAX_LEASES {
            policy.grant(1, 10, 0).unwrap();
        }
        assert!(policy.grant(1, 10, 0).is_err());
        assert!(policy.grant(1, 10, 10).is_ok());
    }
    #[test]
    fn strict_protocol_and_no_remote_grants() {
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

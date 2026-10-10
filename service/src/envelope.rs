//! Canonical, fixed-size Authorization Envelope payload shared with the Broker IPC design.
//! The payload is not self-authenticating: callers must obtain it through authenticated
//! Service IPC and the Service remains authoritative for lease state.

pub const MAGIC: u32 = 0x5644_4145; // "VDAE" in little endian
pub const VERSION: u16 = 1;
pub const WIRE_SIZE: usize = 152;
pub const MAX_FRAMES: u32 = 1_728_000;
pub const MAX_BYTES: u32 = 1_073_741_824;
pub const SCOPE_VIEW: u32 = 1;
pub const SCOPE_RECORD: u32 = 2;
pub const ROLE_BROKER: u32 = 3;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Subject {
    pub pid: u32,
    pub session: u32,
    pub creation_time: u64,
    pub sid_hash: [u8; 32],
    pub image_hash: [u8; 32],
    pub role: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AuthorizationEnvelope {
    pub lease_id: u64,
    pub epoch: u64,
    pub generation: u64,
    pub subject: Subject,
    pub scope: u32,
    pub issued_at: u64,
    pub deadline: u64,
    pub nonce: u64,
    pub max_frames: u32,
    pub max_bytes: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DecodeError {
    InvalidSize,
    InvalidHeader,
    InvalidField,
}

impl AuthorizationEnvelope {
    pub fn encode(&self) -> [u8; WIRE_SIZE] {
        let mut out = [0_u8; WIRE_SIZE];
        let mut offset = 0;
        put_u32(&mut out, &mut offset, MAGIC);
        put_u16(&mut out, &mut offset, VERSION);
        put_u16(&mut out, &mut offset, WIRE_SIZE as u16);
        put_u64(&mut out, &mut offset, self.lease_id);
        put_u64(&mut out, &mut offset, self.epoch);
        put_u64(&mut out, &mut offset, self.generation);
        put_u32(&mut out, &mut offset, self.subject.pid);
        put_u32(&mut out, &mut offset, self.subject.session);
        put_u64(&mut out, &mut offset, self.subject.creation_time);
        put_bytes(&mut out, &mut offset, &self.subject.sid_hash);
        put_bytes(&mut out, &mut offset, &self.subject.image_hash);
        put_u32(&mut out, &mut offset, self.subject.role);
        put_u32(&mut out, &mut offset, self.scope);
        put_u64(&mut out, &mut offset, self.issued_at);
        put_u64(&mut out, &mut offset, self.deadline);
        put_u64(&mut out, &mut offset, self.nonce);
        put_u32(&mut out, &mut offset, self.max_frames);
        put_u32(&mut out, &mut offset, self.max_bytes);
        debug_assert_eq!(offset, WIRE_SIZE);
        out
    }

    pub fn decode(bytes: &[u8]) -> Result<Self, DecodeError> {
        if bytes.len() != WIRE_SIZE {
            return Err(DecodeError::InvalidSize);
        }
        let mut offset = 0;
        if take_u32(bytes, &mut offset)? != MAGIC
            || take_u16(bytes, &mut offset)? != VERSION
            || take_u16(bytes, &mut offset)? as usize != WIRE_SIZE
        {
            return Err(DecodeError::InvalidHeader);
        }
        let envelope = Self {
            lease_id: take_u64(bytes, &mut offset)?,
            epoch: take_u64(bytes, &mut offset)?,
            generation: take_u64(bytes, &mut offset)?,
            subject: Subject {
                pid: take_u32(bytes, &mut offset)?,
                session: take_u32(bytes, &mut offset)?,
                creation_time: take_u64(bytes, &mut offset)?,
                sid_hash: take_array(bytes, &mut offset)?,
                image_hash: take_array(bytes, &mut offset)?,
                role: take_u32(bytes, &mut offset)?,
            },
            scope: take_u32(bytes, &mut offset)?,
            issued_at: take_u64(bytes, &mut offset)?,
            deadline: take_u64(bytes, &mut offset)?,
            nonce: take_u64(bytes, &mut offset)?,
            max_frames: take_u32(bytes, &mut offset)?,
            max_bytes: take_u32(bytes, &mut offset)?,
        };
        if !envelope.valid_fields() || offset != WIRE_SIZE {
            return Err(DecodeError::InvalidField);
        }
        Ok(envelope)
    }

    pub fn valid_fields(&self) -> bool {
        let role_scope_valid =
            self.subject.role == ROLE_BROKER && matches!(self.scope, SCOPE_VIEW | SCOPE_RECORD);
        self.lease_id != 0
            && self.epoch != 0
            && self.epoch != u64::MAX
            && self.generation != 0
            && self.subject.pid != 0
            && self.subject.session != 0
            && self.subject.creation_time != 0
            && self.subject.sid_hash.iter().any(|byte| *byte != 0)
            && self.subject.image_hash.iter().any(|byte| *byte != 0)
            && role_scope_valid
            && self.issued_at < self.deadline
            && self.nonce != 0
            && self.max_frames != 0
            && self.max_frames <= MAX_FRAMES
            && self.max_bytes != 0
            && self.max_bytes <= MAX_BYTES
    }
}

fn put_u16(out: &mut [u8], offset: &mut usize, value: u16) {
    put_bytes(out, offset, &value.to_le_bytes());
}
fn put_u32(out: &mut [u8], offset: &mut usize, value: u32) {
    put_bytes(out, offset, &value.to_le_bytes());
}
fn put_u64(out: &mut [u8], offset: &mut usize, value: u64) {
    put_bytes(out, offset, &value.to_le_bytes());
}
fn put_bytes<const N: usize>(out: &mut [u8], offset: &mut usize, value: &[u8; N]) {
    out[*offset..*offset + N].copy_from_slice(value);
    *offset += N;
}
fn take_array<const N: usize>(bytes: &[u8], offset: &mut usize) -> Result<[u8; N], DecodeError> {
    let end = offset.checked_add(N).ok_or(DecodeError::InvalidSize)?;
    let value = bytes
        .get(*offset..end)
        .ok_or(DecodeError::InvalidSize)?
        .try_into()
        .map_err(|_| DecodeError::InvalidSize)?;
    *offset = end;
    Ok(value)
}
fn take_u16(bytes: &[u8], offset: &mut usize) -> Result<u16, DecodeError> {
    Ok(u16::from_le_bytes(take_array(bytes, offset)?))
}
fn take_u32(bytes: &[u8], offset: &mut usize) -> Result<u32, DecodeError> {
    Ok(u32::from_le_bytes(take_array(bytes, offset)?))
}
fn take_u64(bytes: &[u8], offset: &mut usize) -> Result<u64, DecodeError> {
    Ok(u64::from_le_bytes(take_array(bytes, offset)?))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> AuthorizationEnvelope {
        AuthorizationEnvelope {
            lease_id: 7,
            epoch: 11,
            generation: 13,
            subject: Subject {
                pid: 17,
                session: 19,
                creation_time: 23,
                sid_hash: [0x31; 32],
                image_hash: [0x52; 32],
                role: ROLE_BROKER,
            },
            scope: SCOPE_VIEW,
            issued_at: 29,
            deadline: 31,
            nonce: 37,
            max_frames: 41,
            max_bytes: 43,
        }
    }

    #[test]
    fn canonical_wire_round_trip_and_fixed_size() {
        let envelope = sample();
        let wire = envelope.encode();
        assert_eq!(wire.len(), WIRE_SIZE);
        assert_eq!(&wire[0..8], &[0x45, 0x41, 0x44, 0x56, 1, 0, 152, 0]);
        assert_eq!(AuthorizationEnvelope::decode(&wire), Ok(envelope));
        let shared_fixture = include_str!("../../tests/fixtures/authorization-envelope-v1.hex");
        let fixture = shared_fixture
            .split_ascii_whitespace()
            .map(|byte| u8::from_str_radix(byte, 16).expect("fixture is hexadecimal"))
            .collect::<Vec<_>>();
        assert_eq!(fixture, wire);
        assert_eq!(AuthorizationEnvelope::decode(&fixture), Ok(envelope));
        assert_eq!(
            AuthorizationEnvelope::decode(&wire[..WIRE_SIZE - 1]),
            Err(DecodeError::InvalidSize)
        );
        let mut oversized = wire.to_vec();
        oversized.push(0);
        assert_eq!(
            AuthorizationEnvelope::decode(&oversized),
            Err(DecodeError::InvalidSize)
        );
    }

    #[test]
    fn rejects_downgrade_role_scope_and_invalid_budget() {
        let mut wire = sample().encode();
        wire[4] = 0;
        assert_eq!(
            AuthorizationEnvelope::decode(&wire),
            Err(DecodeError::InvalidHeader)
        );

        let mut envelope = sample();
        envelope.scope = 99;
        assert_eq!(
            AuthorizationEnvelope::decode(&envelope.encode()),
            Err(DecodeError::InvalidField)
        );
        envelope = sample();
        envelope.subject.role = u32::MAX;
        assert_eq!(
            AuthorizationEnvelope::decode(&envelope.encode()),
            Err(DecodeError::InvalidField)
        );
        envelope = sample();
        envelope.max_frames = 0;
        assert_eq!(
            AuthorizationEnvelope::decode(&envelope.encode()),
            Err(DecodeError::InvalidField)
        );
    }
}

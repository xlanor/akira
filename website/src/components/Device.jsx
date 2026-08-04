import styles from './Device.module.css';

const ICONS = {
  playstation: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="2" y="8" width="20" height="9" rx="4.5" />
      <line x1="7" y1="11" x2="7" y2="14" />
      <line x1="5.5" y1="12.5" x2="8.5" y2="12.5" />
      <circle cx="16.5" cy="11.5" r="1" />
      <circle cx="18.5" cy="13.5" r="1" />
    </svg>
  ),
  pc: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="2" y="4" width="20" height="13" rx="2" />
      <line x1="8" y1="21" x2="16" y2="21" />
      <line x1="12" y1="17" x2="12" y2="21" />
    </svg>
  ),
  switch: (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="2" y="5" width="20" height="14" rx="3" />
      <line x1="8" y1="5" x2="8" y2="19" />
      <line x1="16" y1="5" x2="16" y2="19" />
      <circle cx="5.1" cy="12" r="1.25" fill="currentColor" stroke="none" />
      <circle cx="18.9" cy="10.4" r="1" fill="currentColor" stroke="none" />
      <circle cx="18.9" cy="13.6" r="1" fill="currentColor" stroke="none" />
    </svg>
  ),
};

const LABELS = {
  playstation: 'PlayStation',
  pc: 'PC',
  switch: 'Switch',
};

export default function Device({ kind }) {
  const icon = ICONS[kind];
  if (!icon) return null;
  return (
    <span className={styles.badge} data-kind={kind}>
      {icon}
      <span className={styles.label}>{LABELS[kind]}</span>
    </span>
  );
}

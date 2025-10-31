import { useState, useEffect } from "react";

export default function Dashboard() {
  const [info, setInfo] = useState({
    cpu: 0,
    mem: 0,
    uptime: 0
  });

  useEffect(() => {
    fetch("/api/status")
      .then(r => r.json())
      .then(setInfo)
      .catch(console.error);
  }, []);

  return (
    <section>
      <h2>Dashboard</h2>
      <p><b>CPU:</b> {info.cpu}%</p>
      <p><b>Memória:</b> {info.mem}%</p>
      <p><b>Uptime:</b> {info.uptime}s</p>
    </section>
  );
}

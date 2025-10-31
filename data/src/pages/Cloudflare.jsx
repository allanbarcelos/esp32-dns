import { useState } from "react";

export default function Cloudflare() {
  const [form, setForm] = useState({
    cf_token: "",
    cf_zone: "",
    cf_record: "",
    cf_host: ""
  });

  const handleChange = e => {
    setForm({ ...form, [e.target.name]: e.target.value });
  };

  const handleSubmit = e => {
    e.preventDefault();
    fetch("/api/cloudflare", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(form)
    });
  };

  return (
    <section>
      <h2>Cloudflare Config</h2>
      <form onSubmit={handleSubmit}>
        <label>
          Token
          <input type="password" name="cf_token" value={form.cf_token} onChange={handleChange} required />
        </label>
        <label>
          Zone
          <input type="text" name="cf_zone" value={form.cf_zone} onChange={handleChange} required />
        </label>
        <label>
          Record
          <input type="text" name="cf_record" value={form.cf_record} onChange={handleChange} required />
        </label>
        <label>
          Host
          <input type="text" name="cf_host" value={form.cf_host} onChange={handleChange} required />
        </label>
        <button type="submit">Salvar</button>
      </form>
    </section>
  );
}
